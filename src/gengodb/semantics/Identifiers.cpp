#include "gengodb/semantics/Identifiers.h"
#include "gengodb/semantics/RdfGraph.h"

namespace gengodb::semantics {
using namespace rdf4cpp;

namespace {

struct NodeIdEntry {
    RDFNodeType type;
    int32_t prefixId;
    std::string suffix;

    void serialize(lingodb::utility::Serializer& serializer) const {
        serializer.writeProperty(1, type);
        serializer.writeProperty(2, prefixId);
        serializer.writeProperty(3, suffix);
    }
    static NodeIdEntry deserialize(lingodb::utility::Deserializer& deserializer) {
        auto type = deserializer.readProperty<RDFNodeType>(1);
        auto prefixId = deserializer.readProperty<int32_t>(2);
        auto suffix = deserializer.readProperty<std::string>(3);
        return NodeIdEntry{type, prefixId, std::move(suffix)};
    }
}; // NodeIdEntry

struct NodeIdStore {
    std::vector<std::string> prefixes;
    std::vector<NodeIdEntry> entries;
}; // NodeIdStore

static std::pair<std::string, std::string> splitIRI(const std::string& iri) {
    auto pos = iri.find_last_of("/#");
    if (pos == std::string::npos) return {"", iri};
    return {iri.substr(0, pos + 1), iri.substr(pos + 1)};
}
static NodeIdStore buildStore(const std::vector<NodeId>& nodes) {
    NodeIdStore store;
    std::unordered_map<std::string, int32_t> prefixIndex;
    store.entries.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (node.type == RDFNodeType::IRI) {
            std::string s{node.iri.identifier()};
            auto [prefix, suffix] = splitIRI(s);
            auto it = prefixIndex.find(prefix);
            int32_t id;
            if (it == prefixIndex.end()) {
                id = static_cast<int32_t>(store.prefixes.size());
                store.prefixes.push_back(prefix);
                prefixIndex.emplace(prefix, id);
            }
            else {
                id = it->second;
            }
            store.entries.push_back(NodeIdEntry{node.type, id, std::move(suffix)});
        }
        else if (node.type == RDFNodeType::BNode || node.type == RDFNodeType::Variable) {
            store.entries.push_back(NodeIdEntry{node.type, -1, node.localId});
        }
        else {
            store.entries.push_back(NodeIdEntry{node.type, -1, std::string()});
        }
    }
    return store;
}
struct GlobalLiteralKey {
    const char* data;
    size_t len;
    int64_t datatypeGlobalId;
    bool operator==(const GlobalLiteralKey& other) const noexcept {
        return datatypeGlobalId == other.datatypeGlobalId && std::string(data, len) == std::string(other.data, other.len);
    }
};
struct GlobalLiteralKeyHash {
    std::size_t operator()(const GlobalLiteralKey& k) const noexcept {
        return std::hash<int64_t>{}(k.datatypeGlobalId) ^ (std::hash<std::string>{}(std::string(k.data, k.len)) << 1);
    }
};

static std::vector<NodeId> readStore(const NodeIdStore& store) {
    std::vector<NodeId> nodes;
    nodes.reserve(store.entries.size());
    for (const auto& entry : store.entries) {
        if (entry.type == RDFNodeType::IRI) {
            std::string full = store.prefixes[entry.prefixId] + entry.suffix;
            nodes.push_back(NodeId{entry.type, IRI{full}, std::string()});
        }
        else if (entry.type == RDFNodeType::BNode || entry.type == RDFNodeType::Variable) {
            nodes.push_back(NodeId{entry.type, IRI{}, entry.suffix});
        }
        else {
            nodes.push_back(NodeId{entry.type, IRI{}, std::string()});
        }
    }
    return nodes;
}

} // end anonymous namespace

void NodeIdDict::serialize(lingodb::utility::Serializer& serializer) const {
    const auto& store = buildStore(id_to_node);
    serializer.writeProperty(1, store.prefixes);
    serializer.writeProperty(2, store.entries);
}
std::unique_ptr<NodeIdDict> NodeIdDict::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto prefixes = deserializer.readProperty<std::vector<std::string>>(1);
    auto entries = deserializer.readProperty<std::vector<NodeIdEntry>>(2);
    auto nodes = readStore(NodeIdStore{std::move(prefixes), std::move(entries)});
    auto result = std::make_unique<NodeIdDict>();
    result->id_to_node = std::move(nodes);
    for (size_t id = 0; id < result->id_to_node.size(); id++) {
        result->node_to_id.emplace(result->id_to_node[id], static_cast<int32_t>(id));
    }
    return result;
}

void NodeIdMapping::serialize(lingodb::utility::Serializer& serializer) const {
    serializer.writeProperty(1, local_to_global);
}
std::unique_ptr<NodeIdMapping> NodeIdMapping::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto local_to_global = deserializer.readProperty<std::vector<global_id_t>>(1);
    auto result = std::make_unique<NodeIdMapping>();
    result->local_to_global = std::move(local_to_global);
    for (size_t i = 0; i < result->local_to_global.size(); i++) {
        result->global_to_local.emplace(result->local_to_global[i], static_cast<local_id_t>(i));
    }
    return result;
}

std::unique_ptr<GraphNodeIndex> GraphNodeIndex::build(const std::vector<std::pair<std::string, const RdfGraph*>>& rdfGraphs) {
    auto index = std::make_unique<GraphNodeIndex>();
    NodeIdMapping::global_id_t nextGlobalId = 0;
    std::unordered_map<NodeId, NodeIdMapping::global_id_t, NodeId::NodeIdHash> iriToGlobal;
    std::unordered_map<GlobalLiteralKey, NodeIdMapping::global_id_t, GlobalLiteralKeyHash> literalToGlobal;
    for (const auto& [explicitName, graph] : rdfGraphs) {
        std::string key = explicitName.empty() ? std::string{graph->getIri().identifier()} : explicitName;
        const auto& nodes = graph->getNodes();
        std::vector<NodeIdMapping::global_id_t> globalIds(nodes.size(), -1);
        for (int32_t id = 0; id < static_cast<int32_t>(nodes.size()); id++) {
            const auto nodeId = nodes.get(id);
            if (nodeId.type == RDFNodeType::IRI) {
                auto it = iriToGlobal.find(nodeId);
                if (it == iriToGlobal.end()) {
                    NodeIdMapping::global_id_t globalId = nextGlobalId++;
                    iriToGlobal.emplace(nodeId, globalId);
                    globalIds[id] = globalId;
                } else {
                    globalIds[id] = it->second;
                }
            } else if (nodeId.type == RDFNodeType::BNode || nodeId.type == RDFNodeType::Variable) {
                // Blank nodes/variables are graph-local by spec: never deduplicated.
                globalIds[id] = nextGlobalId++;
            }
        }
        NodeHelper helper(const_cast<RdfGraph*>(graph));
        for (int32_t id = 0; id < static_cast<int32_t>(nodes.size()); id++) {
            if (nodes.get(id).type != RDFNodeType::Literal) continue;
            const LiteralKey localKey = helper.literalKeyFor(id);
            const NodeIdMapping::global_id_t datatypeGlobalId = globalIds[localKey.dataType];
            GlobalLiteralKey litKey{localKey.data, localKey.len, datatypeGlobalId};
            auto it = literalToGlobal.find(litKey);
            if (it == literalToGlobal.end()) {
                NodeIdMapping::global_id_t globalId = nextGlobalId++;
                literalToGlobal.emplace(litKey, globalId);
                globalIds[id] = globalId;
            } else {
                globalIds[id] = it->second;
            }
        }
        auto mapping = std::make_unique<NodeIdMapping>();
        for (const auto gid : globalIds) {
            mapping->insert(gid);
        }
        index->index[std::move(key)] = std::move(mapping);
    }
    return index;
}

void GraphNodeIndex::serialize(lingodb::utility::Serializer& serializer) const {
    serializer.writeProperty(1, index);
}
std::unique_ptr<GraphNodeIndex> GraphNodeIndex::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto index = deserializer.readProperty<std::unordered_map<std::string, std::unique_ptr<NodeIdMapping>>>(1);
    auto result = std::make_unique<GraphNodeIndex>();
    result->index = std::move(index);
    return result;
}

} // namespace gengodb::semantics