#include "gengodb/semantics/RdfGraph.h"

#include "gengodb/semantics/RdfFileFormat.h"

#include <rdf4cpp/Graph.hpp>
#include <rdf4cpp/parser/RDFFileParser.hpp>

#include <cstdio>

#define GENGODB_DEFAULT_CAPACITY 1024

namespace gengodb::semantics {
using namespace rdf4cpp::parser;

inline void NodeHelper::ensureNode() {
    if (static_cast<size_t>(g->storage->storage().nodeHighWater()) < g->nodes->size()) {
        g->storage->storage().addNode();
    }
}
inline int32_t NodeHelper::resolve(const IRI& iri) {
    auto id = g->nodes->get_or_insert(iri);
    ensureNode();
    return id;
}
inline int32_t NodeHelper::resolve(const BlankNode& b) {
    auto id = g->nodes->get_or_insert(b);
    ensureNode();
    return id;
}
inline int32_t NodeHelper::resolve(const Literal& l) {
    const IRI datatype = l.datatype();
    RdfDatatypeInlineHelper inlineHelper;
    RdfDatatypeFixedHelper fixedHelper;
    const bool inlined = inlineHelper.isInlined(datatype);
    const std::optional<RdfDatatypeFixedHelper::Kind> fixedKind = inlined ? std::nullopt : fixedHelper.kindOf(datatype);

    std::string data;
    uint32_t inlineBits = 0;
    int64_t fixedI64 = 0;
    uint64_t fixedUI64 = 0;
    double fixedDouble = 0;
    if (inlined) {
        inlineHelper.inlineValue(&inlineBits, l.value(), datatype);
        data.assign(reinterpret_cast<const char*>(&inlineBits), sizeof(inlineBits));
    } 
    else if (fixedKind) {
        const auto anyValue = l.value();
        switch (*fixedKind) {
            case RdfDatatypeFixedHelper::Kind::Int64:
                fixedI64 = fixedHelper.extract<int64_t>(anyValue);
                data.assign(reinterpret_cast<const char*>(&fixedI64), sizeof(fixedI64));
                break;
            case RdfDatatypeFixedHelper::Kind::UInt64:
                fixedUI64 = fixedHelper.extract<uint64_t>(anyValue);
                data.assign(reinterpret_cast<const char*>(&fixedUI64), sizeof(fixedUI64));
                break;
            case RdfDatatypeFixedHelper::Kind::Double:
                fixedDouble = fixedHelper.extract<double>(anyValue);
                data.assign(reinterpret_cast<const char*>(&fixedDouble), sizeof(fixedDouble));
                break;
        }
    } 
    else {
        const auto lang = l.language_tag();
        assert(lang.size() <= 0xff && "language tag too long to persist");
        data.push_back(static_cast<char>(static_cast<uint8_t>(lang.size())));
        data.append(lang.data(), lang.size());
        const auto lex = l.lexical_form();
        data.append(lex.data(), lex.size());
    }

    LiteralKey key{std::string(datatype.identifier()), data};
    if (auto it = g->literalNodes.find(key); it != g->literalNodes.end())
        return it->second;

    int32_t id = g->nodes->insert(l);
    ensureNode();

    int32_t datatypeNode = resolve(datatype);
    uint32_t xsdType = static_cast<uint32_t>(xsd::from_iri(datatype));
    uint32_t value = 0;
    if (inlined) {
        value = inlineBits;
    } else if (fixedKind) {
        auto& propData = g->storage->storage().getPropData();
        switch (*fixedKind) {
            case RdfDatatypeFixedHelper::Kind::Int64:
                value = static_cast<uint32_t>(propData.add_i64(fixedI64));
                break;
            case RdfDatatypeFixedHelper::Kind::UInt64:
                value = static_cast<uint32_t>(propData.add_ui64(fixedUI64));
                break;
            case RdfDatatypeFixedHelper::Kind::Double:
                value = static_cast<uint32_t>(propData.add_double(fixedDouble));
                break;
        }
    } else {
        auto [ptr, idx] = g->storage->storage().getPropData().add_blob<xsd::Type::String>(data.size());
        std::memcpy(ptr, data.data(), data.size());
        value = static_cast<uint32_t>(idx);
    }
    g->storage->storage().addNodeProperty(id, static_cast<uint32_t>(datatypeNode), xsdType, value);
    g->literalNodes.emplace(std::move(key), id);
    return id;
}
void RdfGraph::addTriple(const IRI& s, const IRI& p, const IRI& o) {
    storage->storage().addRelationship(nodeHelper.resolve(s), nodeHelper.resolve(o), nodeHelper.resolve(p));
}
void RdfGraph::addTriple(const IRI& s, const IRI& p, const BlankNode& o) {
    storage->storage().addRelationship(nodeHelper.resolve(s), nodeHelper.resolve(o), nodeHelper.resolve(p));
}
void RdfGraph::addTriple(const IRI& s, const IRI& p, const Literal& o) {
    storage->storage().addRelationship(nodeHelper.resolve(s), nodeHelper.resolve(o), nodeHelper.resolve(p));
}
void RdfGraph::addTriple(const BlankNode& s, const IRI& p, const IRI& o) {
    storage->storage().addRelationship(nodeHelper.resolve(s), nodeHelper.resolve(o), nodeHelper.resolve(p));
}
void RdfGraph::addTriple(const BlankNode& s, const IRI& p, const BlankNode& o) {
    storage->storage().addRelationship(nodeHelper.resolve(s), nodeHelper.resolve(o), nodeHelper.resolve(p));
}
void RdfGraph::addTriple(const BlankNode& s, const IRI& p, const Literal& o) {
    storage->storage().addRelationship(nodeHelper.resolve(s), nodeHelper.resolve(o), nodeHelper.resolve(p));
}
void RdfGraph::loadTriples() {
    if (!loadedFromRdfFile) {
        return;
    }
    RDFFileParser parser(dbDir + fileName + getRDFFileExtension(rdfParseFlags), rdfParseFlags);
    for (const auto &v : parser) {
        if (!v.has_value())
            break;
        auto quad = v.value();
        this->addTriple(quad.subject(), quad.predicate(), quad.object());
    }
}
std::unique_ptr<RdfGraph> RdfGraph::create(const std::string& name, const IRI& iri) {
    auto storage = runtime::GengoDBGraph::create(name);
    auto rdfGraph = std::make_unique<RdfGraph>(iri.null() ? extra_namespaces().GENGODB + name : iri, std::move(storage), name);
    return rdfGraph;
}
void RdfGraph::flush() {
    if (loadedFromRdfFile) {
        return;
    }
    storage->flush();
}
void RdfGraph::ensureLoaded() {
    if (!loaded) {
        loaded = true;
        if (loadedFromRdfFile) {
            storage = std::make_unique<runtime::GengoDBGraph>(fileName,
                GENGODB_DEFAULT_CAPACITY, GENGODB_DEFAULT_CAPACITY, GENGODB_DEFAULT_CAPACITY);
            storage->setDBDir(dbDir);
            // The graph is rebuilt from scratch below, so any node dictionary
            // restored from a stale catalog snapshot must be discarded too.
            nodes = std::make_unique<NodeIdDict>();
            literalNodes.clear();
            loadTriples();
        }
        storage->ensureLoaded();
        storage->storage().getMetadata().set_name(iri.identifier().data());
        storage->storage().getMetadata().set_identifier_mapping([&](int32_t id) {
            auto nodeId = getNodes().get(id);
            switch(nodeId.type) {
                case RDFNodeType::IRI: return static_cast<std::string>(nodeId.iri);
                case RDFNodeType::BNode: return "_:" + nodeId.localId;
                case RDFNodeType::Literal: return static_cast<std::string>(getLiteral(id));
                default: return std::string("UNKNOWN");
            }
        });
    }
}
Literal RdfGraph::getLiteral(int32_t id) const {
    const auto& n = storage->storage().node(id);
    if (n.payload < 0) {
        assert(false && "literal node missing data property");
        return Literal{};
    }
    const auto& p = storage->storage().prop(n.payload);
    const IRI datatype = getIri(static_cast<int32_t>(p.key));

    RdfDatatypeInlineHelper inlineHelper;
    if (inlineHelper.isInlined(datatype)) {
        return Literal::make_typed(inlineHelper.toLexicalForm(p.value, datatype), datatype);
    }

    RdfDatatypeFixedHelper fixedHelper;
    if (const auto fixedKind = fixedHelper.kindOf(datatype)) {
        auto& propData = storage->storage().getPropData();
        const auto idx = static_cast<int32_t>(p.value);
        std::string lex;
        switch (*fixedKind) {
            case RdfDatatypeFixedHelper::Kind::Int64:
                lex = std::to_string(propData.get_i64(idx));
                break;
            case RdfDatatypeFixedHelper::Kind::UInt64:
                lex = std::to_string(propData.get_ui64(idx));
                break;
            case RdfDatatypeFixedHelper::Kind::Double: {
                // std::to_string truncates to 6 fractional digits; %.17g is required
                // for a double to always round-trip exactly through its decimal text.
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", propData.get_double(idx));
                lex = buf;
                break;
            }
        }
        return Literal::make_typed(lex, datatype);
    }

    auto [ptr, len] = storage->storage().getPropData().get_blob<xsd::Type::String>(static_cast<int32_t>(p.value));
    const char* raw = reinterpret_cast<const char*>(ptr);
    const uint8_t langLen = static_cast<uint8_t>(raw[0]);
    const std::string_view lang(raw + 1, langLen);
    const std::string_view lex(raw + 1 + langLen, len - 1 - langLen);
    if (!lang.empty()) {
        return Literal::make_lang_tagged(lex, lang);
    }
    return Literal::make_typed(lex, datatype);
}
void RdfGraph::serialize(lingodb::utility::Serializer& serializer) const {
    serializer.writeProperty(1, iri.identifier());
    serializer.writeProperty(2, storage);
    serializer.writeProperty(3, fileName);
    serializer.writeProperty(4, nodes);
}
std::unique_ptr<RdfGraph> RdfGraph::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto iri = deserializer.readProperty<std::string>(1);
    auto storage = deserializer.readProperty<std::unique_ptr<lingodb::runtime::GengoDBGraph>>(2);
    auto fileName = deserializer.readProperty<std::string>(3);
    auto nodes = deserializer.readProperty<std::unique_ptr<NodeIdDict>>(4);
    auto graph = std::make_unique<RdfGraph>(IRI{iri}, std::move(storage), fileName);
    graph->nodes = std::move(nodes);
    return graph;
}

} // lingodb::semantics