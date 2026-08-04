#ifndef GENGODB_RUNTIME_BUILTINGRAPHS_H
#define GENGODB_RUNTIME_BUILTINGRAPHS_H

#include "gengodb/runtime/Graph.h"

#include "gengodb/semantics/Datatypes.h"

#include <cstdio>

namespace gengodb::semantics {
    class RdfGraph;
}

namespace lingodb::runtime {
using namespace gengodb::semantics;
struct BuiltinGraph {
    enum Type {
        BUILTIN_SIMPLE_GRAPH = 0,
        BUILTIN_PROPERTY_GRAPH = 1,
        BUILTIN_ALIGN_8X3_8X1_GRAPH = 2
    };
    static int64_t typeId(const void* ptr);
    static Type type(const void* ptr);
};

class SimpleGraph {
public:
    const BuiltinGraph::Type TYPE = BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH;
    using Base = Graph<uint64_t, uint64_t>;
    using NodeEntry = Base::NodeEntry;
    using RelEntry  = Base::RelEntry;

    SimpleGraph(int32_t nodeCapacity, int32_t relCapacity)
        : graph_(nodeCapacity, relCapacity),
        nodeCap_(nodeCapacity), relCap_(relCapacity) {}

    node_id_t addNode(uint64_t value = 0) { return graph_.addNode(value); }
    rel_id_t addRelationship(node_id_t from, node_id_t to, uint64_t value = 0) {
        return graph_.addRelationship(from, to, /*typeId=*/0, value);
    }
    void removeNode(node_id_t id) { graph_.removeNode(id); }
    void removeRelationship(rel_id_t id) { graph_.removeRelationship(id); }

    void setNodeValue(node_id_t id, uint64_t v) { graph_.node(id).payload = v; }
    uint64_t getNodeValue(node_id_t id) const { return graph_.node(id).payload; }
    void setRelValue(rel_id_t id, uint64_t v) { graph_.rel(id).payload = v; }
    uint64_t getRelValue(rel_id_t id) const { return graph_.rel(id).payload; }

    NodeEntry& node(node_id_t id) const { return graph_.node(id); }
    RelEntry& rel(rel_id_t id) const { return graph_.rel(id); }
    NodeEntry& newNode() { return graph_.newNode(); }
    RelEntry& newRel() { return graph_.newRel(); }

    uint8_t* nodeStorePtr() const { return graph_.nodeStorePtr(); }
    uint8_t* relStorePtr() const { return graph_.relStorePtr(); }
    int32_t nodeHighWater() const { return graph_.nodeHighWater(); }
    int32_t relHighWater() const { return graph_.relHighWater(); }
    size_t freeNodes() const { return graph_.freeNodes(); }
    size_t freeRels() const { return graph_.freeRels(); }

    void registerGraph();

    static SimpleGraph* create(int32_t nodeCapacity, int32_t relCapacity);
    static void destroy(SimpleGraph* g) { delete g; }
    void clear() { graph_.clear(); }

private:
   Base graph_;
   int32_t nodeCap_, relCap_;
}; // SimpleGraph

static_assert(sizeof(SimpleGraph::NodeEntry)            == 24);
static_assert(offsetof(SimpleGraph::NodeEntry, payload) == 16);
static_assert(sizeof(SimpleGraph::RelEntry)             == 40);
static_assert(offsetof(SimpleGraph::RelEntry, payload)  == 32);

using prop_id_t = int32_t;

struct BlobTable {
    BlobTable(size_t cap) : blobData_(cap) {}
    ~BlobTable() = default;
    std::pair<std::byte*, int32_t> alloc(size_t len) {
        std::byte* ptr = blobData_.ptr;
        if (!blobs_.empty()) {
            ptr = blobs_.back().first + blobs_.back().second;
        }
        blobs_.push_back(std::make_pair(ptr, len));
        const size_t index = blobs_.size() - 1;
        if (index > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            assert(false && "exceeded int32_t index range");
        }
        return std::make_pair(ptr, static_cast<int32_t>(index));
    }
    std::pair<std::byte*, size_t> get(int32_t idx) const {
        return blobs_[idx];
    }
    template<typename T>
    void store(int32_t idx, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
            "T must be trivially copyable to store as a raw blob");
        auto [ptr, len] = get(idx);
        if (sizeof(T) > len) {
            assert(false && "value size exceeds allocated blob length");
        }
        memcpy(ptr, &value, sizeof(T));
    }
    template<typename T>
    T getValue(int32_t idx) const {
        static_assert(std::is_trivially_copyable_v<T>,
            "T must be trivially copyable to read back from a raw blob");
        auto [ptr, len] = get(idx);
        if (sizeof(T) > len) {
            assert(false && "value size exceeds allocated blob length");
        }
        T value{};
        memcpy(&value, ptr, sizeof(T));
        return value;
    }
    size_t blobCount() const { return blobs_.size(); }
    size_t usedBytes() const {
        if (blobs_.empty()) return 0;
        return static_cast<size_t>(blobs_.back().first - blobData_.ptr) + blobs_.back().second;
    }
    const std::byte* rawData() const { return blobData_.ptr; }
    void restore(const std::byte* raw, size_t rawLen, const std::vector<uint32_t>& lengths) {
        if (rawLen) memcpy(blobData_.ptr, raw, rawLen);
        size_t offset = 0;
        for (auto len : lengths) {
            blobs_.push_back(std::make_pair(blobData_.ptr + offset, static_cast<size_t>(len)));
            offset += len;
        }
    }
private:
    std::vector<std::pair<std::byte*, size_t>> blobs_;
    LegacyFixedSizedBuffer<std::byte> blobData_;
}; // BlobTable

class PropertyGraph {
public:
    using Base = Graph<prop_id_t, prop_id_t>;
    using NodeEntry = Base::NodeEntry;
    using RelEntry  = Base::RelEntry;
    const BuiltinGraph::Type TYPE = BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH;

    struct PropRecord {
        prop_id_t nextPropId;
        prop_id_t prevPropId;
        uint32_t  key;
        uint32_t  type;
        uint32_t  value;
        bool      inUse;
    };

    struct PropertyData {
        using BlobTableT = std::unordered_map<xsd::Type, std::unique_ptr<BlobTable>>;

        inline int64_t     get_i64(int32_t idx) const { return lst_i64_[idx]; }
        inline const int64_t*  get_i64_ptr(int32_t idx) const { return &lst_i64_[idx]; }
        inline int32_t     add_i64(int64_t v) { lst_i64_.push_back(v); return static_cast<int32_t>(lst_i64_.size() - 1); }
        inline uint64_t    get_ui64(int32_t idx) const { return lst_ui64_[idx]; }
        inline const uint64_t* get_ui64_ptr(int32_t idx) const { return &lst_ui64_[idx]; }
        inline int32_t     add_ui64(uint64_t v) { lst_ui64_.push_back(v); return static_cast<int32_t>(lst_ui64_.size() - 1); }
        inline double      get_double(int32_t idx) const { return lst_double_[idx]; }
        inline const double*   get_double_ptr(int32_t idx) const { return &lst_double_[idx]; }
        inline int32_t     add_double(double v) { lst_double_.push_back(v); return static_cast<int32_t>(lst_double_.size() - 1); }

        template<xsd::Type t>
        inline std::pair<std::byte*, size_t> get_blob(int32_t idx) const { return blobs_.at(t)->get(idx); }
        template<xsd::Type t, size_t blob_size = 1024>
        inline std::pair<std::byte*, int32_t> add_blob(size_t len) {
            if (!blobs_.contains(t)) {
                blobs_.insert(std::make_pair(t, std::make_unique<BlobTable>(blob_size)));
            }
            return blobs_.at(t)->alloc(len);
        }

        void flush(std::FILE* f) const;
        void load(std::FILE* f);

        private:
        std::vector<int64_t> lst_i64_;
        std::vector<uint64_t> lst_ui64_;
        std::vector<double> lst_double_;
        BlobTableT blobs_;
    };
    
    struct Metadata {
        inline void set_identifier_mapping(std::function<std::string(int32_t)> identifier) { identifier_ = identifier; }
        inline void set_uid_mapping(std::function<uint64_t(int32_t)> uid) { uid_ = uid; }
        inline std::string identifier(int32_t id) const { return identifier_(id); }
        inline uint64_t uid(int32_t id) const { return uid_(id); }
        inline const std::string& name() const { return name_; }
        inline void set_name(const std::string& n) { name_ = n; }

        private:
        std::string name_ = "";
        std::function<std::string(int32_t)> identifier_ = [](int32_t i){ return std::to_string(i); };
        std::function<uint64_t(int32_t)> uid_ = [](int32_t i){ return static_cast<uint64_t>(i); };
    };

    PropertyGraph(int32_t nodeCapacity, int32_t relCapacity, int32_t propCapacity);
    ~PropertyGraph() = default;

    node_id_t addNode();
    void removeNode(node_id_t node);
    rel_id_t addRelationship(node_id_t from, node_id_t to, uint32_t typeId);
    void removeRelationship(rel_id_t rel);
    
    prop_id_t addNodeProperty(node_id_t node, uint32_t key, uint32_t type, uint32_t value = 0);
    prop_id_t addRelProperty(rel_id_t rel, uint32_t key, uint32_t type, uint32_t value = 0);
    void setPropertyValue(prop_id_t prop, uint32_t value);
    // Removes prop from its chain; chainHead is updated if prop was the head.
    void removeProperty(prop_id_t prop, prop_id_t& chainHead);

    NodeEntry& node(node_id_t id) const { return graph_.node(id); }
    RelEntry& rel(rel_id_t id) const { return graph_.rel(id); }
    PropRecord& prop(prop_id_t id) const;
    
    NodeEntry& newNode() { return graph_.newNode(); }
    RelEntry& newRel() { return graph_.newRel(); }
    PropRecord& newProp();

    uint8_t* nodeStorePtr() const { return graph_.nodeStorePtr(); }
    uint8_t* relStorePtr() const { return graph_.relStorePtr(); }
    uint8_t* propStorePtr() const { return reinterpret_cast<uint8_t*>(props_.ptr); }
    int32_t nodeHighWater() const { return graph_.nodeHighWater(); }
    int32_t relHighWater() const { return graph_.relHighWater(); }
    int32_t propHighWater() const { return propMark_; }
    size_t freeNodes() const { return graph_.freeNodes(); }
    size_t freeRels() const { return graph_.freeRels(); }
    size_t freeProps() const { return freeProps_.size(); }

    void registerGraph();

    static PropertyGraph* create(int32_t nodeCapacity, int32_t relCapacity, int32_t propCapacity);
    static void destroy(PropertyGraph* g);
    void clear() { graph_.clear(); propMark_ = 0; freeProps_.clear(); }

    PropertyData& getPropData() { return propData_; }
    Metadata& getMetadata() { return metadata_; }

private:
    prop_id_t addPropertyToChain(prop_id_t& chainHead, uint32_t key, uint32_t type, uint32_t value);
    prop_id_t allocProp();

private:
    Base graph_;
    LegacyFixedSizedBuffer<PropRecord> props_;
    int32_t propMark_, propCap_;
    std::vector<prop_id_t> freeProps_;
    int32_t nodeCap_, relCap_;
    PropertyData propData_;
    Metadata metadata_;

}; // PropertyGraph

static_assert(sizeof(PropertyGraph::NodeEntry)               == 16);
static_assert(offsetof(PropertyGraph::NodeEntry, firstRelId) ==  0);
static_assert(offsetof(PropertyGraph::NodeEntry, inUse)      ==  4);
static_assert(offsetof(PropertyGraph::NodeEntry, labels)     ==  5);
static_assert(offsetof(PropertyGraph::NodeEntry, extra)      == 10);
static_assert(offsetof(PropertyGraph::NodeEntry, payload)    == 12); // firstPropId, needs 1 byte padding

static_assert(sizeof(PropertyGraph::RelEntry)                       == 36);
static_assert(offsetof(PropertyGraph::RelEntry, firstNodeId)        == 0);
static_assert(offsetof(PropertyGraph::RelEntry, inUse)              == 28);
static_assert(offsetof(PropertyGraph::RelEntry, firstInChainMarker) == 29);
static_assert(offsetof(PropertyGraph::RelEntry, payload)            == 32); // firstPropId, needs 2 bytes padding


// A graph with payload alignment 8, 24 bytes node data, 8 bytes rel data.
class AlignmentGraph_8x3_8x1 {
public:
    struct Payload {
        uint64_t a;
        uint64_t b;
        uint64_t c;
    };

    const BuiltinGraph::Type TYPE = BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH;
    using Base = Graph<Payload, uint64_t>;
    using NodeEntry = Base::NodeEntry;
    using RelEntry  = Base::RelEntry;

    AlignmentGraph_8x3_8x1(int32_t nodeCapacity, int32_t relCapacity)
        : graph_(nodeCapacity, relCapacity),
          nodeCap_(nodeCapacity), relCap_(relCapacity) {}

    node_id_t addNode() {
        return graph_.addNode(Payload{0, 0, 0});
    }
    rel_id_t addRelationship(node_id_t from, node_id_t to) {
        return graph_.addRelationship(from, to, /*typeId=*/0, /*payload=*/0u);
    }
    void removeNode(node_id_t id)        { graph_.removeNode(id); }
    void removeRelationship(rel_id_t id) { graph_.removeRelationship(id); }

    NodeEntry& node(node_id_t id) const { return graph_.node(id); }
    RelEntry&  rel(rel_id_t id)   const { return graph_.rel(id); }
    NodeEntry& newNode()                { return graph_.newNode(); }
    RelEntry&  newRel()                 { return graph_.newRel(); }

    uint8_t* nodeStorePtr() const  { return graph_.nodeStorePtr(); }
    uint8_t* relStorePtr()  const  { return graph_.relStorePtr(); }
    int32_t  nodeHighWater() const { return graph_.nodeHighWater(); }
    int32_t  relHighWater()  const { return graph_.relHighWater(); }
    size_t   freeNodes() const     { return graph_.freeNodes(); }
    size_t   freeRels()  const     { return graph_.freeRels(); }

    void registerGraph();

    static AlignmentGraph_8x3_8x1* create(int32_t nodeCapacity, int32_t relCapacity);
    static void destroy(AlignmentGraph_8x3_8x1* g) { delete g; }
    void clear() { graph_.clear(); }

private:
    Base graph_;
    int32_t nodeCap_, relCap_;
}; // AlignmentGraph_8x3_8x1

static_assert(sizeof(AlignmentGraph_8x3_8x1::Payload)      == 24);
static_assert(offsetof(AlignmentGraph_8x3_8x1::Payload, a) ==  0);
static_assert(offsetof(AlignmentGraph_8x3_8x1::Payload, b) ==  8);
static_assert(offsetof(AlignmentGraph_8x3_8x1::Payload, c) == 16);

static_assert(sizeof(AlignmentGraph_8x3_8x1::NodeEntry)            == 40);
static_assert(offsetof(AlignmentGraph_8x3_8x1::NodeEntry, payload) == 16);
static_assert(sizeof(AlignmentGraph_8x3_8x1::RelEntry)             == 40);
static_assert(offsetof(AlignmentGraph_8x3_8x1::RelEntry, payload)  == 32);

} // namespace lingodb::runtime

#endif // GENGODB_RUNTIME_BUILTINGRAPHS_H