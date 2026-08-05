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
                return std::hash<std::string>{}(iri.identifier().data());
            }
            case RDFNodeType::BNode:
            case RDFNodeType::Variable:
            default: {
                return std::hash<std::string>{}(localId);
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
            return NodeId{ RDFNodeType::IRI, node.as_iri(), std::string{} };
        else if (node.is_blank_node())
            return NodeId{ RDFNodeType::BNode, IRI{}, node.as_blank_node().identifier().data() };
        return NodeId { RDFNodeType::Literal, IRI{}, std::string{} };
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

class NodeIdMapping {
    public:
    using global_id_t = int64_t;
    using local_id_t = int32_t;
    private:
    std::vector<global_id_t> local_to_global;
    std::unordered_map<global_id_t, local_id_t> global_to_local;
    public:
    NodeIdMapping() = default;
    ~NodeIdMapping() = default;
    local_id_t insert(const global_id_t gid) {
        local_id_t lid = static_cast<local_id_t>(local_to_global.size());
        local_to_global.push_back(gid);
        global_to_local.emplace(gid, lid);
        return lid;
    }
    local_id_t get_local_safe(const global_id_t gid) const {
        auto it = global_to_local.find(gid);
        if (it == global_to_local.end())
            return -1;
        return it->second;
    }
    local_id_t get_local(const global_id_t gid) const {
        return global_to_local.at(gid);
    }
    global_id_t get_global_safe(const local_id_t id) const {
        if (id < 0 || static_cast<size_t>(id) >= local_to_global.size()) {
            return -1;
        }
        return local_to_global[id];
    }
    global_id_t get_global(const local_id_t id) const {
        return local_to_global[id];
    }
    size_t size() const { return local_to_global.size(); }
    void serialize(utility::Serializer& serializer) const;
    static std::unique_ptr<NodeIdMapping> deserialize(utility::Deserializer& deserializer);
}; // NodeIdMapping

class RdfGraph;
class GraphNodeIndex {
    std::unordered_map<std::string, std::unique_ptr<NodeIdMapping>> index;
    public:
    GraphNodeIndex() = default;
    ~GraphNodeIndex() = default;

    const NodeIdMapping& getIndex(const std::string& graphName) const { return *index.at(graphName); }
    bool hasIndex(const std::string& graphName) const { return index.contains(graphName); }

    std::vector<std::string> getGraphNames() const {
        std::vector<std::string> names;
        names.reserve(index.size());
        for (const auto& [name, mapping] : index) {
            names.push_back(name);
        }
        return names;
    }

    static std::unique_ptr<GraphNodeIndex> build(const std::vector<std::pair<std::string, const RdfGraph*>>& rdfGraphs);

    void serialize(utility::Serializer& serializer) const;
    static std::unique_ptr<GraphNodeIndex> deserialize(utility::Deserializer& deserializer);
}; // GraphNodeIndex

} // namespace gengodb::semantics

#endif // GENGODB_SEMANTICS_IDENTIFIERS_H