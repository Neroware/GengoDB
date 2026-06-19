#ifndef GENGODB_RUNTIME_GRAPHDATA_H
#define GENGODB_RUNTIME_GRAPHDATA_H

#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {
using VarLen32 = lingodb::runtime::VarLen32;
struct Neo4JGraph;
struct GraphData {
    static uint8_t* allocAndPopulateBuiltinGraph(int32_t builtin);
    static SimpleGraph* allocSimpleGraphState(size_t nodeBufLen, size_t relBufLen);
    static PropertyGraph* allocPropertyGraphState(size_t nodeBufLen, size_t relBufLen, size_t propBufLen);
    static void createGraph(VarLen32 meta);
    static PropertyGraph* getGraph(VarLen32 name, VarLen32 iri);
    static std::unique_ptr<Neo4JGraph> serialize(const PropertyGraph& pg);
    static void deserialize(PropertyGraph& dst, const Neo4JGraph& g);
}; // GraphHelper

// Neo4J doubly-linked adjacency-list property graph following the storage
// model described in "Graph Databases" (Robinson, Webber & Eifrem, 2nd ed.,
// Figure 6-4).
struct Neo4JGraph {
    Neo4JGraph(size_t nNodes, size_t nRels, size_t nProps)
        : nNodes(nNodes), nRels(nRels), nProps(nProps), nodes(nNodes), rels(nRels), props(nProps) {}
    // PropRecord: fixed size (21 bytes)
    struct __attribute__((packed)) PropRecord {
        bool      inUse;
        prop_id_t nextPropId;
        prop_id_t prevPropId;
        uint32_t  key;
        uint32_t  type;
        uint32_t  value;
    };
    // NodeEntry: fixed size (15 bytes)
    struct __attribute__((packed)) NodeEntry {
        bool        inUse;
        node_id_t   firstRelId;
        uint32_t    firstPropId;
        uint8_t     labels[5] = {0};
        uint8_t     extra = 0;
    };
    // RelEntry: fixed size (34 bytes) 
    struct __attribute__((packed)) RelEntry {
        bool       inUse;
        node_id_t  firstNodeId;
        node_id_t  secondNodeId;
        rel_type_t typeId;
        rel_id_t   firstPrevRelId;
        rel_id_t   firstNextRelId;
        rel_id_t   secondPrevRelId;
        rel_id_t   secondNextRelId;
        uint32_t   firstPropId;
        uint8_t    firstInChainMarker = 0;
    };
    size_t nNodes, nRels, nProps;
    LegacyFixedSizedBuffer<NodeEntry> nodes;
    LegacyFixedSizedBuffer<RelEntry> rels;
    LegacyFixedSizedBuffer<PropRecord> props;
};

static_assert(sizeof(Neo4JGraph::NodeEntry)                == 15);
static_assert(offsetof(Neo4JGraph::NodeEntry, inUse)       ==  0);
static_assert(offsetof(Neo4JGraph::NodeEntry, firstRelId)  ==  1);
static_assert(offsetof(Neo4JGraph::NodeEntry, firstPropId) ==  5); // firstPropId
static_assert(offsetof(Neo4JGraph::NodeEntry, labels)      ==  9);
static_assert(offsetof(Neo4JGraph::NodeEntry, extra)       == 14);

static_assert(sizeof(Neo4JGraph::RelEntry)                       == 34);
static_assert(offsetof(Neo4JGraph::RelEntry, inUse)              ==  0);
static_assert(offsetof(Neo4JGraph::RelEntry, firstNodeId)        ==  1);
static_assert(offsetof(Neo4JGraph::RelEntry, firstPropId)        == 29); // firstPropId
static_assert(offsetof(Neo4JGraph::RelEntry, firstInChainMarker) == 33);

static_assert(sizeof(Neo4JGraph::PropRecord)                     == 21);
static_assert(offsetof(Neo4JGraph::PropRecord, inUse)            ==  0);
static_assert(offsetof(Neo4JGraph::PropRecord, nextPropId)       ==  1);

struct GraphPropertyData {
    VarLen32 getDummyStr() { return VarLen32::fromString("hello world"); }
}; // GraphPropertyAccess

} // namespace lingodb::runtime

#endif // GENGODB_RUNTIME_GRAPHDATA_H