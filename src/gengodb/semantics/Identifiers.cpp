#include "gengodb/semantics/Identifiers.h"

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

} // namespace gengodb::semantics