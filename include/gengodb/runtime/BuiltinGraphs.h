#ifndef GENGODB_RUNTIME_BUILTINGRAPHS_H
#define GENGODB_RUNTIME_BUILTINGRAPHS_H

#include "gengodb/runtime/Graph.h"

#define N_BYTES_SIMPLE_PROPERTY 24 // Change if needed

namespace lingodb::runtime {
struct BuiltinGraph {
    enum Type {
        BUILTIN_SIMPLE_GRAPH = 0,
        BUILTIN_PROPERTY_GRAPH = 1
    };
    static int64_t typeId(const void* ptr);
    static Type type(const void* ptr);
};

class SimpleGraph {
public:
    const BuiltinGraph::Type TYPE = BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH;
    struct SimpleProp {
        uint8_t data[N_BYTES_SIMPLE_PROPERTY] = {0};
    };
    using Base = Graph<SimpleProp, SimpleProp>;
    using NodeEntry = Base::NodeEntry;
    using RelEntry  = Base::RelEntry;

    SimpleGraph(int32_t nodeCapacity, int32_t relCapacity)
        : graph_(nodeCapacity, relCapacity) {}

    node_id_t addNode(SimpleProp value = {0}) { return graph_.addNode(value); }
    rel_id_t addRelationship(node_id_t from, node_id_t to, SimpleProp value = {0}) {
        return graph_.addRelationship(from, to, /*typeId=*/0, value);
    }
    void removeNode(node_id_t id) { graph_.removeNode(id); }
    void removeRelationship(rel_id_t id) { graph_.removeRelationship(id); }

    void setNodeValue(node_id_t id, SimpleProp v) { graph_.node(id).payload = v; }
    SimpleProp getNodeValue(node_id_t id) const { return graph_.node(id).payload; }
    void setRelValue(rel_id_t id, SimpleProp v) { graph_.rel(id).payload = v; }
    SimpleProp getRelValue(rel_id_t id) const { return graph_.rel(id).payload; }

    NodeEntry& node(node_id_t id) const { return graph_.node(id); }
    RelEntry& rel(rel_id_t id) const { return graph_.rel(id); }

    uint8_t* nodeStorePtr() const { return graph_.nodeStorePtr(); }
    uint8_t* relStorePtr() const { return graph_.relStorePtr(); }
    int32_t nodeHighWater() const { return graph_.nodeHighWater(); }
    int32_t relHighWater() const { return graph_.relHighWater(); }
    size_t freeNodes() const { return graph_.freeNodes(); }
    size_t freeRels() const { return graph_.freeRels(); }

    static SimpleGraph* create(int32_t nodeCapacity, int32_t relCapacity);
    static void destroy(SimpleGraph* g) { delete g; }

private:
   Base graph_;
}; // SimpleGraph

static_assert(sizeof(SimpleGraph::NodeEntry) == 11 + N_BYTES_SIMPLE_PROPERTY);
static_assert(offsetof(SimpleGraph::NodeEntry, payload) == 5);
static_assert(sizeof(SimpleGraph::RelEntry) == 30 + N_BYTES_SIMPLE_PROPERTY);
static_assert(offsetof(SimpleGraph::RelEntry, payload) == 29);

using prop_id_t = int32_t;

class PropertyGraph {
public:
    using Base = Graph<prop_id_t, prop_id_t>;
    using NodeEntry = Base::NodeEntry;
    using RelEntry  = Base::RelEntry;
    const BuiltinGraph::Type TYPE = BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH;

    struct __attribute__((packed)) PropRecord {
        bool  inUse;
        prop_id_t nextPropId;
        prop_id_t prevPropId;
        uint32_t  key;
        uint32_t  type;
        uint32_t  value;
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

    uint8_t* nodeStorePtr() const { return graph_.nodeStorePtr(); }
    uint8_t* relStorePtr() const { return graph_.relStorePtr(); }
    uint8_t* propStorePtr() const { return reinterpret_cast<uint8_t*>(props_.ptr); }
    int32_t nodeHighWater() const { return graph_.nodeHighWater(); }
    int32_t relHighWater() const { return graph_.relHighWater(); }
    int32_t propHighWater() const { return propMark_; }
    size_t freeNodes() const { return graph_.freeNodes(); }
    size_t freeRels() const { return graph_.freeRels(); }
    size_t freeProps() const { return freeProps_.size(); }

    static PropertyGraph* create(int32_t nodeCapacity, int32_t relCapacity, int32_t propCapacity);
    static void destroy(PropertyGraph* g);

private:
    prop_id_t addPropertyToChain(prop_id_t& chainHead, uint32_t key, uint32_t type, uint32_t value);
    prop_id_t allocProp();

private:
    Base graph_;
    LegacyFixedSizedBuffer<PropRecord> props_;
    int32_t propMark_, propCap_;
    std::vector<prop_id_t> freeProps_;

}; // PropertyGraph

static_assert(sizeof(PropertyGraph::NodeEntry)               == 15);
static_assert(offsetof(PropertyGraph::NodeEntry, inUse)      ==  0);
static_assert(offsetof(PropertyGraph::NodeEntry, firstRelId) ==  1);
static_assert(offsetof(PropertyGraph::NodeEntry, payload)    ==  5); // firstPropId
static_assert(offsetof(PropertyGraph::NodeEntry, labels)     ==  9);
static_assert(offsetof(PropertyGraph::NodeEntry, extra)      == 14);

static_assert(sizeof(PropertyGraph::RelEntry)                == 34);
static_assert(offsetof(PropertyGraph::RelEntry, inUse)       ==  0);
static_assert(offsetof(PropertyGraph::RelEntry, firstNodeId) ==  1);
static_assert(offsetof(PropertyGraph::RelEntry, payload)     == 29); // firstPropId
static_assert(offsetof(PropertyGraph::RelEntry, firstInChainMarker) == 33);

} // namespace lingodb::runtime

#endif // GENGODB_RUNTIME_BUILTINGRAPHS_H