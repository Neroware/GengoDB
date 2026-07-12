#include "gengodb/semantics/Identifiers.h"

namespace gengodb::semantics {
using namespace rdf4cpp;

namespace {

struct IRIStore {
    std::vector<std::string> prefixes;
    std::vector<std::pair<int, std::string>> entries;
}; // IriStore

static std::pair<std::string, std::string> splitIRI(const std::string& iri) {
    auto pos = iri.find_last_of("/#");
    if (pos == std::string::npos) return {"", iri};
    return {iri.substr(0, pos + 1), iri.substr(pos + 1)};
}
static IRIStore buildStore(const std::vector<IRI>& iris) {
    IRIStore store;
    std::unordered_map<std::string, int> prefixIndex;
    for (const auto& iri : iris) {
        const std::string& s = iri.identifier().data();
        auto [prefix, suffix] = splitIRI(s);
        auto it = prefixIndex.find(prefix);
        int id;
        if (it == prefixIndex.end()) {
            id = static_cast<int>(store.prefixes.size());
            store.prefixes.push_back(prefix);
            prefixIndex.emplace(prefix, id);
        }
        else {
            id = it->second;
        }
        store.entries.emplace_back(id, std::move(suffix));
    }
    return store;
}
static const std::vector<IRI> readStore(const IRIStore& store) {
    std::vector<IRI> iris;
    iris.reserve(store.entries.size());
    for (const auto& [prefixId, suffix] : store.entries) {
        std::string full = store.prefixes[prefixId] + suffix;
        iris.emplace_back(IRI{full});
    }
    return iris;
}

} // end anonymous namespace

void IRIDict::serialize(lingodb::utility::Serializer& serializer) const {
    const auto& store = buildStore(id_to_node);
    serializer.writeProperty(1, store.prefixes);
    serializer.writeProperty(2, store.entries);
}
std::unique_ptr<IRIDict> IRIDict::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto prefixes = deserializer.readProperty<std::vector<std::string>>(1);
    auto entries = deserializer.readProperty<std::vector<std::pair<int, std::string>>>(2);
    const auto& iris = readStore(IRIStore{prefixes, entries});
    auto result = std::make_unique<IRIDict>();
    result->id_to_node = std::move(iris);
    for (size_t id = 0; id < result->id_to_node.size(); id++) {
        result->node_to_id.emplace(result->id_to_node[id], id);
    }
    return result;
}

void LocalIdDict::serialize(lingodb::utility::Serializer& serializer) const {
    std::vector<std::string> store;
    std::transform(id_to_node.begin(), id_to_node.end(), store.begin(), [](const BlankNode& bnode) -> std::string {
        return bnode.identifier().data();
    });
    serializer.writeProperty(1, store);
}
std::unique_ptr<LocalIdDict> LocalIdDict::deserialize(lingodb::utility::Deserializer& deserializer) {
    auto store = deserializer.readProperty<std::vector<std::string>>(1);
    auto result = std::make_unique<LocalIdDict>();
    std::transform(store.begin(), store.end(), result->id_to_node.begin(), [](const std::string& localId) -> BlankNode {
        return BlankNode{localId};
    });
    for (size_t id = 0; id < result->id_to_node.size(); id++) {
        result->node_to_id.emplace(result->id_to_node[id], id);
    }
    return result;
}

} // namespace gengodb::semantics