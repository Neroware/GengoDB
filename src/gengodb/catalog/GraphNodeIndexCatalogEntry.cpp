#include "gengodb/catalog/GraphNodeIndexCatalogEntry.h"
#include "gengodb/semantics/RdfGraph.h"

namespace gengodb::catalog {

void GraphNodeIndexCatalogEntry::serializeEntry(lingodb::utility::Serializer& serializer) const {
    serializer.writeProperty(2, impl);
}
std::shared_ptr<GraphNodeIndexCatalogEntry> GraphNodeIndexCatalogEntry::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto impl = deserializer.readProperty<std::unique_ptr<semantics::GraphNodeIndex>>(2);
    return std::make_shared<GraphNodeIndexCatalogEntry>(std::move(impl));
}

int64_t GraphNodeIndexCatalogEntry::getGlobalId(const std::string& graphName, int32_t localId) const {
    return getMapping(graphName).get_global_safe(localId);
}
int32_t GraphNodeIndexCatalogEntry::getLocalId(const std::string& graphName, int64_t globalId) const {
    return getMapping(graphName).get_local_safe(globalId);
}

const gengodb::semantics::NodeIdMapping& GraphNodeIndexCatalogEntry::getMapping(const std::string& graphName) const {
    return impl->getIndex(graphName);
}

std::shared_ptr<GraphNodeIndexCatalogEntry> GraphNodeIndexCatalogEntry::build(const std::vector<std::pair<std::string, const semantics::RdfGraph*>>& graphs) {
    auto index = semantics::GraphNodeIndex::build(graphs);
    return std::make_shared<GraphNodeIndexCatalogEntry>(std::move(index));
}

void GraphNodeIndexCatalogEntry::setCatalog(Catalog* catalog) {
    CatalogEntry::setCatalog(catalog);
    if (!catalog || !impl) return;

    for (const auto& name : impl->getGraphNames()) {
        auto graphEntry = catalog->getTypedEntry<RDFGraphCatalogEntry>(name);
        if (!graphEntry) continue;
        const auto& mapping = getMapping(name);
        (*graphEntry)->getGraph().getStorage().storage().getMetadata().set_id_mapping(
            [&mapping](int32_t localId) -> uint64_t {
                return static_cast<uint64_t>(mapping.get_global(localId));
            },
            [&mapping](uint64_t globalId) -> int32_t {
                return mapping.get_local(static_cast<int64_t>(globalId));
            });
    }
}

} // namespace gengodb::catalog
