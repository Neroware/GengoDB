#ifndef GENGODB_SEMANTICS_IDENTIFIERS_H
#define GENGODB_SEMANTICS_IDENTIFIERS_H

#include <rdf4cpp.hpp>
#include <rdf4cpp/storage/identifier/RDFNodeType.hpp>

#include "gengodb/catalog/CreateRdfGraphDef.h"

namespace gengodb::semantics {
using namespace lingodb;
using namespace rdf4cpp;
using namespace rdf4cpp::storage::identifier;

struct NodeId {
    RDFNodeType type;
    IRI iri;
    std::string localId;
    bool operator==(NodeId const& other) const noexcept {
        return type == other.type
            && iri == other.iri
            && localId == other.localId;
    }
    private:
    std::size_t hash() const noexcept {
        switch (type) {
            case RDFNodeType::Literal:
                // Literal nodes have no identifier within an RDF graph
                return 0;
            case RDFNodeType::IRI: {
                std::size_t h1 = std::hash<std::underlying_type_t<RDFNodeType>>{}(
                    static_cast<std::underlying_type_t<RDFNodeType>>(type));
                std::size_t h2 = std::hash<IRI>{}(iri);
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
            case RDFNodeType::BNode:
            case RDFNodeType::Variable:
            default: {
                std::size_t h1 = std::hash<std::underlying_type_t<RDFNodeType>>{}(
                    static_cast<std::underlying_type_t<RDFNodeType>>(type));
                std::size_t h2 = std::hash<std::string>{}(localId);
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
        }
    }
    public:
    struct NodeIdHash {
        std::size_t operator()(gengodb::semantics::NodeId const& id) const noexcept {
            return id.hash();
        }
    };
};

class NodeIdDict {
    private:
    std::unordered_map<NodeId, int32_t, NodeId::NodeIdHash> node_to_id;
    std::vector<NodeId> id_to_node;
    private:
    static inline NodeId node_id(const Node& node) {
        if (node.is_iri())
            return NodeId{ RDFNodeType::IRI, node.as_iri(), std::string() };
        else if (node.is_blank_node())
            return NodeId{ RDFNodeType::BNode, IRI{}, node.as_blank_node().identifier().data() };
        return NodeId { RDFNodeType::Literal, IRI{}, std::string() };
    }
    public:
    NodeIdDict(const std::initializer_list<Node>& l) {
        for (auto it = l.begin(); it != l.end(); it++) {
            int32_t id = static_cast<int32_t>(id_to_node.size());
            id_to_node.push_back(node_id(*it));
            node_to_id.emplace(id_to_node.back(), id);
        }
    }
    NodeIdDict() {}
    ~NodeIdDict() {}
    int32_t insert(const Node& node) {
        int32_t id = static_cast<int32_t>(id_to_node.size());
        id_to_node.push_back(node_id(node));
        node_to_id.emplace(id_to_node.back(), id);
        return id;
    }
    int32_t get_or_insert(const Node& node) {
        auto nid = node_id(node);
        auto it = node_to_id.find(nid);
        if (it != node_to_id.end())
            return it->second;
        int32_t id = static_cast<int32_t>(id_to_node.size());
        id_to_node.push_back(nid);
        node_to_id.emplace(id_to_node.back(), id);
        return id;
    }
    int32_t get_safe(const Node& node) const {
        auto it = node_to_id.find(node_id(node));
        if (it == node_to_id.end())
            return -1;
        return it->second;
    }
    NodeId get(int32_t id) const {
        if (static_cast<size_t>(id) > id_to_node.size())
            return NodeId{};
        return id_to_node[id];
    }
    size_t size() const { return id_to_node.size(); }
    void serialize(utility::Serializer& serializer) const;
    static std::unique_ptr<NodeIdDict> deserialize(utility::Deserializer& deserializer);
}; // NodeIdDict

} // namespace gengodb::semantics

#endif // GENGODB_SEMANTICS_IDENTIFIERS_H