#include "gengodb/semantics/RdfGraph.h"

#include "gengodb/semantics/RdfFileFormat.h"

#include <rdf4cpp/Graph.hpp>
#include <rdf4cpp/parser/RDFFileParser.hpp>

#define GENGODB_DEFAULT_CAPACITY 1024

namespace gengodb::semantics {
using namespace rdf4cpp::parser;

inline void NodeHelper::ensureNode() {
    if (static_cast<size_t>(g->storage->storage().nodeHighWater()) < g->nodes.size()) {
        g->storage->storage().addNode();
    }
}
inline int32_t NodeHelper::resolveNode(const BlankNode& b) {
    const auto ident = b.identifier();
    auto it = g->bnodes.find(ident);
    if (it != g->bnodes.end())
        return it->second;
    auto id = g->nodes.insert(IRI{});
    ensureNode();
    g->bnodes.emplace(ident, id);
    return id;
}
inline int32_t NodeHelper::resolveNode(const IRI& iri) {
    auto id = g->nodes.get_or_insert(iri);
    ensureNode();
    return id;
}
inline int32_t NodeHelper::resolvePredicate(const IRI& p) {
    return g->relations.get_or_insert(p);
}
inline void NodeHelper::addLiteral(int32_t sid, int32_t pid, const Literal& o) {
    RdfDatatypeInlineHelper inlineHelper;
    auto l = o.as_literal();
    auto datatype = g->literalTypes.get_or_insert(l.datatype());
    uint64_t v = 0;
    assert(inlineHelper.isInlined(l.datatype()) && "only inlined literals supported");
    inlineHelper.inlineValue(&v, l.value(), l.datatype());
    g->storage->storage().addNodeProperty(sid, pid, datatype, v);
}
void RdfGraph::addTriple(const IRI& s, const IRI& p, const IRI& o) {
    storage->storage().addRelationship(nodeHelper.resolveNode(s), nodeHelper.resolveNode(o), nodeHelper.resolvePredicate(p));
}
void RdfGraph::addTriple(const IRI& s, const IRI& p, const BlankNode& o) {
    storage->storage().addRelationship(nodeHelper.resolveNode(s), nodeHelper.resolveNode(o), nodeHelper.resolvePredicate(p));
}
void RdfGraph::addTriple(const IRI& s, const IRI& p, const Literal& o) {
    nodeHelper.addLiteral(nodeHelper.resolveNode(s), nodeHelper.resolvePredicate(p), o);
}
void RdfGraph::addTriple(const BlankNode& s, const IRI& p, const IRI& o) {
    storage->storage().addRelationship(nodeHelper.resolveNode(s), nodeHelper.resolveNode(o), nodeHelper.resolvePredicate(p));
}
void RdfGraph::addTriple(const BlankNode& s, const IRI& p, const BlankNode& o) {
    storage->storage().addRelationship(nodeHelper.resolveNode(s), nodeHelper.resolveNode(o), nodeHelper.resolvePredicate(p));
}
void RdfGraph::addTriple(const BlankNode& s, const IRI& p, const Literal& o) {
    nodeHelper.addLiteral(nodeHelper.resolveNode(s), nodeHelper.resolvePredicate(p), o);
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
            storage = std::make_unique<runtime::GengoDBGraph>(name, 
                GENGODB_DEFAULT_CAPACITY, GENGODB_DEFAULT_CAPACITY, GENGODB_DEFAULT_CAPACITY);
            loadTriples();
        }
        storage->ensureLoaded();
    }
}
void RdfGraph::serialize(lingodb::utility::Serializer& serializer) const {
    serializer.writeProperty(1, iri.identifier());
    serializer.writeProperty(2, storage);
    serializer.writeProperty(3, fileName);
}
std::unique_ptr<RdfGraph> RdfGraph::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto iri = deserializer.readProperty<std::string>(1);
    auto storage = deserializer.readProperty<std::unique_ptr<lingodb::runtime::GengoDBGraph>>(2);
    auto fileName = deserializer.readProperty<std::string>(3);
    return std::make_unique<RdfGraph>(IRI{iri}, std::move(storage), fileName);
}

} // lingodb::semantics