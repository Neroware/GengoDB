#ifndef GENGODB_SEMANTICS_RDFGRAPH_H
#define GENGODB_SEMANTICS_RDFGRAPH_H

#include "gengodb/runtime/GengoDBGraph.h"
#include "gengodb/catalog/CreateRdfGraphDef.h"
#include "gengodb/semantics/Identifiers.h"
#include <rdf4cpp.hpp>

#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

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
        else assert(false && "unsupported datatype for inlining.");
    }
    template<typename T>
    void inlineValueImpl(uint32_t* out, const std::any& in) const {
        const T* v = std::any_cast<T>(&in);
        if (!v) {
            assert(false && "bad cast");
        }
        static_assert(sizeof(T) <= sizeof(uint32_t), "Type too large to inline");
        uint32_t tmp = 0;
        std::memcpy(&tmp, v, sizeof(T));
        *out = tmp;
    }
    /**
     * Reconstructs the canonical lexical form of an inlined value, undefined behavior if type cannot be inlined
     */
    std::string toLexicalForm(uint32_t bits, const IRI& datatype) const {
        const Namespace xsd = extra_namespaces().XSD;
        if (datatype == xsd + "boolean")                return toLexicalFormImpl<bool>(bits) ? "true" : "false";
        if (datatype == xsd + "byte")                   return std::to_string(toLexicalFormImpl<int8_t>(bits));
        if (datatype == xsd + "float")                  return std::to_string(toLexicalFormImpl<float>(bits));
        if (datatype == xsd + "int")                    return std::to_string(toLexicalFormImpl<int32_t>(bits));
        if (datatype == xsd + "short")                  return std::to_string(toLexicalFormImpl<int16_t>(bits));
        if (datatype == xsd + "unsignedByte")           return std::to_string(toLexicalFormImpl<uint8_t>(bits));
        if (datatype == xsd + "unsignedInt")            return std::to_string(toLexicalFormImpl<uint32_t>(bits));
        if (datatype == xsd + "unsignedShort")          return std::to_string(toLexicalFormImpl<uint16_t>(bits));
        assert(false && "unsupported datatype for inlining.");
        return "";
    }
    template<typename T>
    T toLexicalFormImpl(uint32_t bits) const {
        static_assert(sizeof(T) <= sizeof(uint32_t), "Type too large to inline");
        T v{};
        std::memcpy(&v, &bits, sizeof(T));
        return v;
    }
};
struct RdfDatatypeFixedHelper {
    enum class Kind { Int64, UInt64, Double };
    std::optional<Kind> kindOf(const IRI& datatype) const {
        const Namespace xsd = extra_namespaces().XSD;
        if (datatype == xsd + "long")          return Kind::Int64;
        if (datatype == xsd + "unsignedLong")  return Kind::UInt64;
        if (datatype == xsd + "double")        return Kind::Double;
        return std::nullopt;
    }
    template<typename T>
    T extract(const std::any& in) const {
        const T* v = std::any_cast<T>(&in);
        if (!v) {
            assert(false && "bad cast");
            return T{};
        }
        return *v;
    }
};
struct LiteralKey {
    std::string datatypeIri;
    std::string data;
    bool operator==(const LiteralKey& other) const noexcept {
        return datatypeIri == other.datatypeIri && data == other.data;
    }
};
struct LiteralKeyHash {
    std::size_t operator()(const LiteralKey& k) const noexcept {
        return std::hash<std::string>{}(k.datatypeIri) ^ (std::hash<std::string>{}(k.data) << 1);
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
    std::unique_ptr<NodeIdDict> nodes;
    std::unordered_map<LiteralKey, int32_t, LiteralKeyHash> literalNodes;
public:
    RdfGraph(const IRI& iri, std::unique_ptr<runtime::GengoDBGraph> storage, std::string fileName) 
        : iri(iri), storage(std::move(storage)), nodes(std::make_unique<NodeIdDict>()), persist(false), fileName(std::move(fileName)), loadedFromRdfFile(false), rdfParseFlags(parser::ParsingFlag::Turtle), nodeHelper(this) {}
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
        storage->setDBDir(dbDir);
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

        }
        else if (s.is_blank_node()) {
            const auto& subj = s.as_blank_node();

            if (o.is_iri())                 addTriple(subj, pred, o.as_iri());
            else if (o.is_blank_node())     addTriple(subj, pred, o.as_blank_node());
            else if (o.is_literal())        addTriple(subj, pred, o.as_literal());
            else assert(false && "triple object invalid");

        }
        else {
            assert(false && "triple subject invalid");
        }
    }
    void addTriple(const IRI& s, const IRI& p, const IRI& o);
    void addTriple(const IRI& s, const IRI& p, const BlankNode& o);
    void addTriple(const IRI& s, const IRI& p, const Literal& o);
    void addTriple(const BlankNode& s, const IRI& p, const IRI& o);
    void addTriple(const BlankNode& s, const IRI& p, const BlankNode& o);
    void addTriple(const BlankNode& s, const IRI& p, const Literal& o);
    IRI getIri() const { return iri; }
    const NodeIdDict& getNodes() const { return *nodes; }
    RDFNodeType getNodeType(int32_t id) const { return nodes->get(id).type; }
    IRI getIri(int32_t id) const { return nodes->get(id).iri; }
    BlankNode getBNode(int32_t id) const { return BlankNode{nodes->get(id).localId}; }
    Literal getLiteral(int32_t id) const;
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

#endif // GENGODB_SEMANTICS_RDFGRAPH_H