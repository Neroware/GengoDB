#include "gengodb/compiler/Dialect/GraphSubOp/NamedGraphManager.h"

namespace gengodb::compiler::dialect::gsubop {

void NamedGraphManager::addNamedGraph(std::string name, std::shared_ptr<RdfGraph> graph) {
    namedGraphs_[name] = graph;
}
int32_t NamedGraphManager::resolve(std::string name, std::string identifier) const {
    if (namedGraphs_.find(name) != namedGraphs_.end()) {
        return namedGraphs_.at(name)->getNodes().get_safe(IRI{identifier});
    }
    return 0;
}
std::string NamedGraphManager::getUniqueId(std::string name) const {
    if (namedGraphs_.find(name) != namedGraphs_.end()) {
        return namedGraphs_.at(name)->getIri().identifier().data();
    }
    return "";
}

} // namespace gengodb::compiler::dialect::gsubop