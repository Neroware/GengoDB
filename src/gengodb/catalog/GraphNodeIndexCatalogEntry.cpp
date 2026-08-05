#include "gengodb/catalog/GraphNodeIndexCatalogEntry.h"

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

std::shared_ptr<GraphNodeIndexCatalogEntry> GraphNodeIndexCatalogEntry::build(const std::vector<std::shared_ptr<RDFGraphCatalogEntry>>& graphs) {
    auto idx = std::make_unique<semantics::GraphNodeIndex>();
    for (const auto& entry : graphs) {
        if (!entry) continue;
        entry->addToIndex(*idx);
    }
    idx->build();
    return std::make_shared<GraphNodeIndexCatalogEntry>(std::move(idx));
}

} // namespace gengodb::catalog
