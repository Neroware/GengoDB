#include "gengodb/compiler/Dialect/GraphSubOp/NamedGraphManager.h"

namespace gengodb::compiler::dialect::gsubop {

void NamedGraphManager::addNamedGraph(std::string name, std::string uid, std::shared_ptr<gengodb::catalog::RDFGraphCatalogEntry> graph) {
    namedGraphs_[name] = std::make_pair(std::move(uid), std::move(graph));
}
int32_t NamedGraphManager::resolve(std::string name, std::string identifier) const {
    if (namedGraphs_.find(name) != namedGraphs_.end()) {
        return namedGraphs_.at(name).second->getNodes().get_safe(IRI{identifier});
    }
    return 0;
}
std::string NamedGraphManager::getUniqueId(std::string name) const {
    if (namedGraphs_.find(name) != namedGraphs_.end()) {
        return namedGraphs_.at(name).first;
    }
    return "";
}

} // namespace gengodb::compiler::dialect::gsubop