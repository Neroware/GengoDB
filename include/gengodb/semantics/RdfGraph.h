#ifndef GENGODB_RDFGRAPH_H
#define GENGODB_RDFGRAPH_H

#include "gengodb/runtime/GengoDBGraph.h"
#include "gengodb/catalog/CreateRdfGraphDef.h"
#include <rdf4cpp.hpp>

#include <iostream>

namespace lingodb::runtime {
    class GengoDBGraph;
}

namespace gengodb::semantics {
using namespace rdf4cpp;
using namespace lingodb;

struct extra_namespaces {
    const Namespace PI = Namespace("https://www.uni-augsburg.de/de/fakultaet/fai/informatik/prof/pi#");
    const Namespace SOBOT = Namespace("https://www.forsocialrobots.de/ontologies/sobots.owl#");
    const Namespace LINGODB = Namespace("https://www.lingo-db.com/rdf#");
    const Namespace GENGODB = Namespace("https://github.com/Neroware/LingoDB#");
    const Namespace XSD = Namespace("http://www.w3.org/2001/XMLSchema#");
};
class IriDictionary {
private:
    std::unordered_map<IRI, int32_t> iri_to_id;
    std::vector<IRI> id_to_iri;
public:
    IriDictionary(const std::initializer_list<IRI>& l) {
        for (auto it = l.begin(); it != l.end(); it++) {
            int32_t id = static_cast<int32_t>(id_to_iri.size());
            id_to_iri.push_back(*it);
            iri_to_id.emplace(id_to_iri.back(), id);
        }
    }
    IriDictionary() {}
    ~IriDictionary() {}
    int32_t insert(const IRI& iri) {
        int32_t id = static_cast<int32_t>(id_to_iri.size());
        id_to_iri.push_back(iri);
        iri_to_id.emplace(id_to_iri.back(), id);
        return id;
    }
    int32_t get_or_insert(const IRI& iri) {
        auto it = iri_to_id.find(iri);
        if (it != iri_to_id.end())
            return it->second;
        int32_t id = static_cast<int32_t>(id_to_iri.size());
        id_to_iri.push_back(iri);
        iri_to_id.emplace(id_to_iri.back(), id);
        return id;
    }
    int32_t get_safe(const IRI& iri) const {
        auto it = iri_to_id.find(iri);
        if (it == iri_to_id.end())
            return -1;
        return it->second;
    }
    IRI get_iri(int32_t id) const {
        if (static_cast<size_t>(id) > id_to_iri.size())
            return IRI{};
        return id_to_iri[id];
    }
    size_t size() const { return id_to_iri.size(); }
}; // IriDictionary
class RdfGraph;
struct RdfDatatypeInlineHelper {
    /**
     * RDF datatype IRIs that can be inlined into the graph storage's property table
     */
    const std::unordered_set<IRI> inlinedIRIs = {
        IRI(datatypes::xsd::Boolean::identifier),
        IRI(datatypes::xsd::Byte::identifier),
        IRI(datatypes::xsd::Float::identifier),
        IRI(datatypes::xsd::Int::identifier),
        IRI(datatypes::xsd::Short::identifier),
        IRI(datatypes::xsd::UnsignedByte::identifier),
        IRI(datatypes::xsd::UnsignedInt::identifier),
        IRI(datatypes::xsd::UnsignedShort::identifier),
    };
    /**
     * Checks if a literal value is inlined into the property table
     */
    bool isInlined(const IRI& datatype) const {
        auto it = inlinedIRIs.find(datatype);
        return it != inlinedIRIs.end();
    }
    /**
     * Writes the inlined value into 'out', undefined behavior if type cannot be inlined
     */
    void inlineValue(uint32_t* out, const std::any& in, const IRI& datatype) const {
        const Namespace xsd = extra_namespaces().XSD;
        if (datatype == xsd + "boolean")                inlineValueImpl<bool>(out, in);
        else if (datatype == xsd + "byte")              inlineValueImpl<int8_t>(out, in);
        else if (datatype == xsd + "float")             inlineValueImpl<float>(out, in);
        else if (datatype == xsd + "int")               inlineValueImpl<int32_t>(out, in);
        else if (datatype == xsd + "short")             inlineValueImpl<int16_t>(out, in);
        else if (datatype == xsd + "unsignedByte")      inlineValueImpl<uint8_t>(out, in);
        else if (datatype == xsd + "unsignedInt")       inlineValueImpl<uint32_t>(out, in);
        else if (datatype == xsd + "unsignedShort")     inlineValueImpl<uint16_t>(out, in);
        else throw std::invalid_argument(("Unsupported datatype for inlining."));
    }
    template<typename T>
    void inlineValueImpl(uint32_t* out, const std::any& in) const {
        const T* v = std::any_cast<T>(&in);
        if (!v) {
            throw std::bad_any_cast();
        }
        static_assert(sizeof(T) <= sizeof(uint32_t), "Type too large to inline");
        uint32_t tmp = 0;
        std::memcpy(&tmp, v, sizeof(T));
        *out = tmp;
    }
};
class NodeHelper {
private:
    RdfGraph* g;
public:
    NodeHelper(RdfGraph* g) : g(g) {}
    inline void ensureNode();
    inline int32_t resolve(const BlankNode& b);
    inline int32_t resolve(const IRI& iri);
    inline int32_t resolve(const Literal& l);
};
class RdfGraph {
private:
    IRI iri;
    std::unique_ptr<runtime::GengoDBGraph> storage;
    IriDictionary nodes;
    std::unordered_map<std::string_view, int32_t> bnodes;
public:
    RdfGraph(const IRI& iri, std::unique_ptr<runtime::GengoDBGraph> storage, std::string fileName) 
        : iri(iri), storage(std::move(storage)), persist(false), fileName(std::move(fileName)), loadedFromRdfFile(false), rdfParseFlags(parser::ParsingFlag::Turtle), nodeHelper(this) {}
    void setPersist(bool persist) {
        this->persist = persist;
        if (persist) {
            flush();
        }
    }
    virtual ~RdfGraph() = default;
    runtime::GengoDBGraph& getStorage() const { return *storage; }
    // flushes the data to disk
    void flush();
    // ensures that the data is loaded
    void ensureLoaded();
    virtual void setDBDir(std::string dbDir) {
        this->dbDir = dbDir;
    }
    virtual void setLoadedFromRdfFile(bool loadedFromRdfFile) {
        this->loadedFromRdfFile = loadedFromRdfFile;
    }
    virtual void setRdfParseFlags(parser::ParsingFlag rdfParseFlags) {
        this->rdfParseFlags = rdfParseFlags;
    }
    static std::unique_ptr<RdfGraph> create(const std::string& name, const IRI& iri);
    void loadTriples();
    void addTriple(const Node& s, const Node& p, const Node& o) {
        if (!p.is_iri()) assert(false && "predicate must be an IRI");
        const auto& pred = p.as_iri();
        if (s.is_iri()) {
            const auto& subj = s.as_iri();
            if (o.is_iri())                 addTriple(subj, pred, o.as_iri());
            else if (o.is_blank_node())     addTriple(subj, pred, o.as_blank_node());
            else if (o.is_literal())        addTriple(subj, pred, o.as_literal());
            else assert(false && "triple object invalid");

        } else if (s.is_blank_node()) {
            const auto& subj = s.as_blank_node();

            if (o.is_iri())                 addTriple(subj, pred, o.as_iri());
            else if (o.is_blank_node())     addTriple(subj, pred, o.as_blank_node());
            else if (o.is_literal())        addTriple(subj, pred, o.as_literal());
            else assert(false && "triple object invalid");

        } else {
            assert(false && "triple subject invalid");
        }
    }
    void addTriple(const IRI& s, const IRI& p, const IRI& o);
    void addTriple(const IRI& s, const IRI& p, const BlankNode& o);
    void addTriple(const IRI& s, const IRI& p, const Literal& o);
    void addTriple(const BlankNode& s, const IRI& p, const IRI& o);
    void addTriple(const BlankNode& s, const IRI& p, const BlankNode& o);
    void addTriple(const BlankNode& s, const IRI& p, const Literal& o);
    IRI getUniqueId() const { return iri; }
    const IriDictionary& getRdfNodes() const { return nodes; }
    const std::unordered_map<std::string_view, int32_t>& getBlankNodes() const { return bnodes; }
    void serialize(lingodb::utility::Serializer& serializer) const;
    static std::unique_ptr<RdfGraph> deserialize(lingodb::utility::Deserializer& deserializer);
private:
    bool persist;
    std::string fileName;
    std::string dbDir;
    bool loadedFromRdfFile;
    parser::ParsingFlag rdfParseFlags;
    
    bool loaded = false;

    NodeHelper nodeHelper;
    friend class NodeHelper;
};

}

#endif // GENGODB_RDFGRAPH_H