#ifndef GENGODB_SEMANTICS_IDENTIFIERS_H
#define GENGODB_SEMANTICS_IDENTIFIERS_H

#include <rdf4cpp.hpp>
#include "gengodb/catalog/CreateRdfGraphDef.h"

namespace gengodb::semantics {
using namespace rdf4cpp;

template <class NodeT>
class NodeIdDict {
protected:
    std::unordered_map<NodeT, int32_t> node_to_id;
    std::vector<NodeT> id_to_node;
public:
    NodeIdDict(const std::initializer_list<NodeT>& l) {
        for (auto it = l.begin(); it != l.end(); it++) {
            int32_t id = static_cast<int32_t>(id_to_node.size());
            id_to_node.push_back(*it);
            node_to_id.emplace(id_to_node.back(), id);
        }
    }
    NodeIdDict() {}
    ~NodeIdDict() {}
    int32_t insert(const NodeT& node) {
        int32_t id = static_cast<int32_t>(id_to_node.size());
        id_to_node.push_back(node);
        node_to_id.emplace(id_to_node.back(), id);
        return id;
    }
    int32_t get_or_insert(const NodeT& node) {
        auto it = node_to_id.find(node);
        if (it != node_to_id.end())
            return it->second;
        int32_t id = static_cast<int32_t>(id_to_node.size());
        id_to_node.push_back(node);
        node_to_id.emplace(id_to_node.back(), id);
        return id;
    }
    int32_t get_safe(const NodeT& node) const {
        auto it = node_to_id.find(node);
        if (it == node_to_id.end())
            return -1;
        return it->second;
    }
    NodeT get(int32_t id) const {
        if (static_cast<size_t>(id) > id_to_node.size())
            return NodeT{};
        return id_to_node[id];
    }
    size_t size() const { return id_to_node.size(); }
}; // NodeIdDict

class IRIDict : public NodeIdDict<IRI> {
    void serialize(lingodb::utility::Serializer& serializer) const;
    static std::unique_ptr<IRIDict> deserialize(lingodb::utility::Deserializer& deserializer);
};
class LocalIdDict : public NodeIdDict<BlankNode> {
    void serialize(lingodb::utility::Serializer& serializer) const;
    static std::unique_ptr<LocalIdDict> deserialize(lingodb::utility::Deserializer& deserializer);
};

} // namespace gengodb::semantics

#endif // GENGODB_SEMANTICS_IDENTIFIERS_H