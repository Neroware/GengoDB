#ifndef LINGODB_RUNTIME_GRAPH_H
#define LINGODB_RUNTIME_GRAPH_H

#include "lingodb/runtime/Buffer.h"
#include "lingodb/runtime/helpers.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace lingodb::runtime {

using node_id_t = int32_t;
using rel_id_t  = int32_t;
using rel_type_t  = int32_t;
static constexpr int32_t GRAPH_NONE = -1;

// Generic doubly-linked adjacency-list property graph following the storage
// model described in "Graph Databases" (Robinson, Webber & Eifrem, 2nd ed.,
// Figure 6-4).
template <typename NodePayload, typename RelPayload>
class Graph {
public:
    // NodeEntry: fixed base (5 bytes) followed by the node payload.
    struct __attribute__((packed)) NodeEntry {
        bool        inUse;
        node_id_t   firstRelId;
        NodePayload payload;
        uint8_t labels[5] = {0};
        uint8_t extra = 0;
    };
    // RelEntry: fixed base (29 bytes) followed by the rel payload.
    struct __attribute__((packed)) RelEntry {
        bool       inUse;
        node_id_t  firstNodeId;
        node_id_t  secondNodeId;
        rel_type_t typeId;
        rel_id_t   firstPrevRelId;
        rel_id_t   firstNextRelId;
        rel_id_t   secondPrevRelId;
        rel_id_t   secondNextRelId;
        RelPayload payload;
        uint8_t    firstInChainMarker = 0;
    };
    Graph(int32_t nodeCapacity, int32_t relCapacity)
      : nodes_(nodeCapacity), rels_(relCapacity),
        nodeMark_(0), relMark_(0),
        nodeCap_(nodeCapacity), relCap_(relCapacity) {}
   ~Graph() = default;
    NodeEntry& node(node_id_t id) const {
        assert(id >= 0 && id < nodeMark_);
        return nodes_.ptr[id];
    }
    RelEntry& rel(rel_id_t id) const {
        assert(id >= 0 && id < relMark_);
        return rels_.ptr[id];
    }
    uint8_t* nodeStorePtr() const { return reinterpret_cast<uint8_t*>(nodes_.ptr); }
    uint8_t* relStorePtr() const { return reinterpret_cast<uint8_t*>(rels_.ptr); }
    int32_t nodeHighWater() const { return nodeMark_; }
    int32_t relHighWater() const { return relMark_; }
    node_id_t addNode(NodePayload initPayload) {
        node_id_t id = allocNode();
        NodeEntry& n = nodes_.ptr[id];
        n.inUse = true;
        n.firstRelId = GRAPH_NONE;
        n.payload = initPayload;
        return id;
    }
    rel_id_t addRelationship(node_id_t from, node_id_t to, uint32_t typeId, RelPayload initPayload) {
        rel_id_t id = allocRel();
        RelEntry& r = rels_.ptr[id];
        r.inUse = true;
        r.firstNodeId = from;
        r.secondNodeId = to;
        r.typeId = typeId;
        r.firstPrevRelId = GRAPH_NONE;
        r.firstNextRelId = GRAPH_NONE;
        r.secondPrevRelId = GRAPH_NONE;
        r.secondNextRelId = GRAPH_NONE;
        r.payload = initPayload;
        linkRelToNode(id, from, /*nodeIsFirst=*/true);
        if (from != to)
            linkRelToNode(id, to, /*nodeIsFirst=*/false);
        return id;
   }
   void removeRelationship(rel_id_t id) {
        RelEntry& r = rels_.ptr[id];
        assert(r.inUse);
        unlinkRelFromNode(id, r.firstNodeId);
        if (r.firstNodeId != r.secondNodeId)
            unlinkRelFromNode(id, r.secondNodeId);
        r.inUse = false;
        freeRels_.push_back(id);
   }
   // Removes all relationships incident to the node, then frees the node.
   // Does NOT clean up relationship/node payloads.
   void removeNode(node_id_t id) {
        NodeEntry& n = nodes_.ptr[id];
        assert(n.inUse);
        rel_id_t cur = n.firstRelId;
        while (cur != GRAPH_NONE) {
            RelEntry& r = rels_.ptr[cur];
            rel_id_t next = (r.firstNodeId == id) ? r.firstNextRelId : r.secondNextRelId;
            removeRelationship(cur);
            cur = next;
        }
        n.inUse = false;
        n.firstRelId = GRAPH_NONE;
        freeNodes_.push_back(id);
   }

private:
    node_id_t allocNode() {
        if (!freeNodes_.empty()) {
            auto id = freeNodes_.back();
            freeNodes_.pop_back();
            return id;
        }
        assert(nodeMark_ < nodeCap_ && "node capacity exceeded");
        return nodeMark_++;
    }
    rel_id_t allocRel() {
        if (!freeRels_.empty()) {
            auto id = freeRels_.back();
            freeRels_.pop_back();
            return id;
        }
        assert(relMark_ < relCap_ && "rel capacity exceeded");
        return relMark_++;
    }
    void linkRelToNode(rel_id_t relId, node_id_t nodeId, bool nodeIsFirst) {
        NodeEntry& n = nodes_.ptr[nodeId];
        RelEntry& r = rels_.ptr[relId];
        rel_id_t oldHead = n.firstRelId;
        if (nodeIsFirst) {
            r.firstNextRelId = oldHead;
            r.firstPrevRelId = GRAPH_NONE;
        } 
        else {
            r.secondNextRelId = oldHead;
            r.secondPrevRelId = GRAPH_NONE;
        }
        if (oldHead != GRAPH_NONE) {
            RelEntry& head = rels_.ptr[oldHead];
            if (head.firstNodeId == nodeId)
                head.firstPrevRelId = relId;
            else
                head.secondPrevRelId = relId;
        }
        
        n.firstRelId = relId;
   }
   void unlinkRelFromNode(rel_id_t relId, node_id_t nodeId) {
        RelEntry& r = rels_.ptr[relId];
        NodeEntry& n = nodes_.ptr[nodeId];
        bool isFirst = (r.firstNodeId == nodeId);
        rel_id_t prevId = isFirst ? r.firstPrevRelId : r.secondPrevRelId;
        rel_id_t nextId = isFirst ? r.firstNextRelId : r.secondNextRelId;
        if (prevId != GRAPH_NONE) {
            RelEntry& prev = rels_.ptr[prevId];
            if (prev.firstNodeId == nodeId) { 
                prev.firstNextRelId  = nextId;
            }
            else {
                prev.secondNextRelId = nextId;
            }
        } 
        else {
            n.firstRelId = nextId;
        }
        if (nextId != GRAPH_NONE) {
            RelEntry& next = rels_.ptr[nextId];
            if (next.firstNodeId == nodeId) {
                next.firstPrevRelId = prevId;
            }
            else {
                next.secondPrevRelId = prevId;
            }
        }
   }

private:
   LegacyFixedSizedBuffer<NodeEntry> nodes_;
   LegacyFixedSizedBuffer<RelEntry> rels_;
   int32_t nodeMark_, relMark_;
   int32_t nodeCap_,  relCap_;
   std::vector<node_id_t> freeNodes_;
   std::vector<rel_id_t> freeRels_;
}; // Graph

} // namespace lingodb::runtime

#endif // LINGODB_RUNTIME_GRAPH_H