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
    }
    impl->ensureLoaded();
}
void RDFGraphCatalogEntry::setShouldPersist(bool shouldPersist) {
    impl->setPersist(shouldPersist);
}
void RDFGraphCatalogEntry::setDBDir(std::string dbDir) {
    impl->setDBDir(dbDir);
}
std::shared_ptr<RDFGraphCatalogEntry> RDFGraphCatalogEntry::createFromCreateRdfGraphDef(const CreateRdfGraphDef& def) {
    std::unique_ptr<RdfGraph> impl = RdfGraph::create(def.name, def.iri);
    auto res = std::make_shared<RDFGraphCatalogEntry>(def.name, std::move(impl), def.format);
    return res;
}

} // namespace gengodb::catalog