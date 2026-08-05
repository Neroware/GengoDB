#ifndef GENGODB_GRAPHNODEINDEXCATALOGENTRY_H
#define GENGODB_GRAPHNODEINDEXCATALOGENTRY_H

#include "lingodb/catalog/Catalog.h"
#include "gengodb/semantics/Identifiers.h"
#include "gengodb/catalog/GraphCatalogEntry.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gengodb::semantics {
class RdfGraph;
} // namespace gengodb::semantics

namespace gengodb::catalog {
using namespace lingodb::catalog;

class GraphNodeIndexCatalogEntry : public CatalogEntry {
    std::unique_ptr<semantics::GraphNodeIndex> impl;

    public:
    static constexpr const char* ENTRY_NAME = "__gengodb_node_index__";

    static constexpr std::array<CatalogEntryType, 1> entryTypes = {CatalogEntryType::GENGODB_NODE_INDEX_ENTRY};

    explicit GraphNodeIndexCatalogEntry(std::unique_ptr<semantics::GraphNodeIndex> impl)
        : CatalogEntry(CatalogEntryType::GENGODB_NODE_INDEX_ENTRY), impl(std::move(impl)) {}

    std::string getName() override { return ENTRY_NAME; }
    void serializeEntry(lingodb::utility::Serializer& serializer) const override;
    static std::shared_ptr<GraphNodeIndexCatalogEntry> deserialize(lingodb::utility::Deserializer& deserializer);
    ~GraphNodeIndexCatalogEntry() override = default;

    void setCatalog(Catalog* catalog) override;

    int64_t getGlobalId(const std::string& graphName, int32_t localId) const;
    int32_t getLocalId(const std::string& graphName, int64_t globalId) const;

    const gengodb::semantics::NodeIdMapping& getMapping(const std::string& graphName) const;

    static std::shared_ptr<GraphNodeIndexCatalogEntry> build(const std::vector<std::pair<std::string, const semantics::RdfGraph*>>& graphs);
};
} // namespace gengodb::catalog

#endif // GENGODB_GRAPHNODEINDEXCATALOGENTRY_H
