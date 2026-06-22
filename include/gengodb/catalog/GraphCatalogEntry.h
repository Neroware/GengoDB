#ifndef GENGODB_GRAPHCATALOGENTRY_H
#define GENGODB_GRAPHCATALOGENTRY_H

#include "lingodb/catalog/Catalog.h"
#include "gengodb/semantics/RdfGraph.h"
#include "gengodb/semantics/RdfFileFormat.h"
#include "gengodb/catalog/CreateRdfGraphDef.h"

#include <rdf4cpp.hpp>

namespace gengodb::semantics {
class RdfGraph;
} // namespace gengodb::semantics

namespace gengodb::catalog {
using namespace lingodb::catalog;
using namespace rdf4cpp;
class GraphCatalogEntry : public CatalogEntry {
protected:
    std::string name;
public:
    static constexpr std::array<CatalogEntryType, 1> entryTypes = {CatalogEntryType::GENGODB_GRAPH_ENTRY};
    GraphCatalogEntry(CatalogEntryType entryType, std::string name) : CatalogEntry(entryType), name(name) {}
    std::string getName() override { return name; }
    size_t nodeCount() { return getStorage().nodeHighWater(); }
    size_t edgeCount() { return getStorage().relHighWater(); }
    size_t propertyCount() { return getStorage().propHighWater(); }
    virtual lingodb::runtime::PropertyGraph& getStorage() = 0;
};

class RDFGraphCatalogEntry : public GraphCatalogEntry {
    std::unique_ptr<semantics::RdfGraph> impl;

    semantics::RDFFileFormat format;

    public:
    RDFGraphCatalogEntry(std::string name, std::unique_ptr<semantics::RdfGraph> impl, semantics::RDFFileFormat format);

    static constexpr std::array<CatalogEntryType, 1> entryTypes = {CatalogEntryType::GENGODB_GRAPH_ENTRY};
    void serializeEntry(lingodb::utility::Serializer& serializer) const override;
    static std::shared_ptr<RDFGraphCatalogEntry> deserialize(lingodb::utility::Deserializer& deserializer);
    ~RDFGraphCatalogEntry() override = default;
    IRI getGraphIri() const;
    IRI getIri(int32_t node) const;
    std::string_view getLocalId(int32_t node) const;
    lingodb::runtime::PropertyGraph& getStorage() override;
    semantics::RDFFileFormat getFormat() const { return format; }
    virtual void flush() override;
    virtual void ensureFullyLoaded() override;
    virtual void setShouldPersist(bool shouldPersist) override;
    virtual void setDBDir(std::string dbDir) override;
    static std::shared_ptr<RDFGraphCatalogEntry> createFromCreateRdfGraphDef(const CreateRdfGraphDef& def);
};
} // lingodb::semantics

#endif // GENGODB_GRAPHCATALOGENTRY_H