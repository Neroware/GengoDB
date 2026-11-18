#include "gengodb/catalog/GraphCatalogEntry.h"

namespace gengodb::catalog {
using namespace gengodb::semantics;

RDFGraphCatalogEntry::RDFGraphCatalogEntry(std::string name, std::unique_ptr<semantics::RdfGraph> impl, semantics::RDFFileFormat format) : GraphCatalogEntry(CatalogEntryType::GENGODB_GRAPH_ENTRY, name), impl(std::move(impl)), format(format) {}

void RDFGraphCatalogEntry::serializeEntry(lingodb::utility::Serializer& serializer) const {
    serializer.writeProperty(2, name);
    serializer.writeProperty(3, impl);
    serializer.writeProperty(4, (int) format);
}
std::shared_ptr<RDFGraphCatalogEntry> RDFGraphCatalogEntry::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto name = deserializer.readProperty<std::string>(2);
    auto rdfGraph = deserializer.readProperty<std::unique_ptr<semantics::RdfGraph>>(3);
    auto format = deserializer.readProperty<int>(4);

    return std::make_shared<RDFGraphCatalogEntry>(name, std::move(rdfGraph), (RDFFileFormat) format);
}
IRI RDFGraphCatalogEntry::getIri() const {
    return impl->getIri();
}
Node RDFGraphCatalogEntry::getNode(int32_t node) const {
    switch(impl->getNodeType(node)) {
        case RDFNodeType::IRI: return impl->getIri(node);
        case RDFNodeType::BNode: return impl->getBNode(node);
        case RDFNodeType::Literal: return impl->getLiteral(node);
        default: return BlankNode{};
    }
}
const gengodb::semantics::NodeIdDict& RDFGraphCatalogEntry::getNodes() const {
    return impl->getNodes();
}
std::string_view RDFGraphCatalogEntry::getLocalId(int32_t node) const {
    switch(impl->getNodeType(node)) {
        case RDFNodeType::IRI: return impl->getIri(node).identifier();
        case RDFNodeType::BNode: return impl->getBNode(node).identifier();
        default: return "";
    }
}
lingodb::runtime::PropertyGraph& RDFGraphCatalogEntry::getStorage() {
    return impl->getStorage().storage();
}
void RDFGraphCatalogEntry::flush() {
    impl->flush();
}    
void RDFGraphCatalogEntry::ensureFullyLoaded() {
    if (format == RDFFileFormat::BINARY) {
        impl->setLoadedFromRdfFile(false);
    }
    else {
        impl->setLoadedFromRdfFile(true);
        impl->setRdfParseFlags(getRDFParseFlags(format));
        impl->setFuzzy(format == RDFFileFormat::FUZZY_TURTLE);
    }
    impl->ensureLoaded();
}
size_t RDFGraphCatalogEntry::getNumNodes() const {
    return impl->getNodes().size();
}
size_t RDFGraphCatalogEntry::getNumTriples() const {
    return impl->getTotalStatistics().getNumTriples();
}
std::vector<std::pair<std::string, int32_t>> RDFGraphCatalogEntry::getPredicates() const {
    std::vector<std::pair<std::string, int32_t>> predicates;
    predicates.reserve(impl->getPredicateStatistics().size());
    for (const auto& [id, stats] : impl->getPredicateStatistics()) {
        predicates.emplace_back(std::string(impl->getIri(id).identifier()), id);
    }
    return predicates;
}
const TripleStatistics& RDFGraphCatalogEntry::getTotalStatistics() const {
    return impl->getTotalStatistics();
}
std::optional<int32_t> RDFGraphCatalogEntry::resolvePredicate(std::string_view iri) const {
    int32_t id = impl->getNodes().get_safe(IRI{iri});
    if (id < 0) return std::nullopt;
    return id;
}
const TripleStatistics* RDFGraphCatalogEntry::getPredicateStatistics(int32_t predicate) const {
    const auto& stats = impl->getPredicateStatistics();
    auto it = stats.find(predicate);
    if (it == stats.end()) return nullptr;
    return &it->second;
}
void RDFGraphCatalogEntry::setShouldPersist(bool shouldPersist) {
    impl->setPersist(shouldPersist);
}
void RDFGraphCatalogEntry::setDBDir(std::string dbDir) {
    impl->setDBDir(dbDir);
}
std::shared_ptr<RDFGraphCatalogEntry> RDFGraphCatalogEntry::createFromCreateRdfGraphDef(const CreateRdfGraphDef& def) {
    std::unique_ptr<RdfGraph> impl = RdfGraph::create(def.name, def.iri, def.sourceFileName, def.nodeCapacity, def.relCapacity, def.propCapacity);
    auto res = std::make_shared<RDFGraphCatalogEntry>(def.name, std::move(impl), def.format);
    return res;
}

} // namespace gengodb::catalog