#ifndef GENGODB_COMPILER_DIALECT_GRAPHSUBOP_NAMEDGRAPHMANAGER_H
#define GENGODB_COMPILER_DIALECT_GRAPHSUBOP_NAMEDGRAPHMANAGER_H

#include "gengodb/catalog/GraphCatalogEntry.h"

#include "llvm/ADT/StringMap.h"
namespace gengodb::compiler::dialect::gsubop {
using namespace gengodb::semantics;
class NamedGraphManager {
    public:
    NamedGraphManager() = default;
    NamedGraphManager(const NamedGraphManager&) = delete;
    NamedGraphManager& operator=(const NamedGraphManager&) = delete;

    void addNamedGraph(std::string name, std::string uid, std::shared_ptr<gengodb::catalog::RDFGraphCatalogEntry> graph);
    int32_t resolve(std::string name, std::string identifier) const;
    std::string getUniqueId(std::string name) const;

    private:
    llvm::StringMap<std::pair<std::string, std::shared_ptr<gengodb::catalog::RDFGraphCatalogEntry>>> namedGraphs_;
};
} // namespace gengodb::compiler::dialect::gsubop

#endif //GENGODB_COMPILER_DIALECT_GRAPHSUBOP_NAMEDGRAPHMANAGER_H
