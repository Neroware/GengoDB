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
IRI RDFGraphCatalogEntry::getGraphIri() const {
    return impl->getUniqueId();
}
IRI RDFGraphCatalogEntry::getIri(int32_t node) const {
    return impl->getRdfNodes().get_iri(node);
}
std::string_view RDFGraphCatalogEntry::getLocalId(int32_t node) const {
    for (const auto& pair : impl->getBlankNodes()) {
        if (pair.second == node) 
            return pair.first;
    }
    return "";
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