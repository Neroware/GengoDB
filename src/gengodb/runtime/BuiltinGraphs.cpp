#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {

SimpleGraph* SimpleGraph::create(int32_t nodeCapacity, int32_t relCapacity) {
    return new SimpleGraph(nodeCapacity, relCapacity);
}

BuiltinGraph::Type BuiltinGraph::type(void* ptr) {
    return *reinterpret_cast<const BuiltinGraph::Type*>(ptr);
}
int64_t BuiltinGraph::typeId(void* ptr) {
    return static_cast<int64_t>(type(ptr));
}

PropertyGraph::PropertyGraph(int32_t nodeCapacity, int32_t relCapacity, int32_t propCapacity)
   : graph_(nodeCapacity, relCapacity),
     props_(propCapacity),
     propMark_(0), propCap_(propCapacity) {}

PropertyGraph::PropRecord& PropertyGraph::prop(prop_id_t id) const {
    assert(id >= 0 && id < propMark_);
    return props_.ptr[id];
}
node_id_t PropertyGraph::addNode() {
    node_id_t id = graph_.addNode(/*initPayload=*/GRAPH_NONE);
    node(id).inUse = true;
    return id;
}
void PropertyGraph::removeNode(node_id_t nodeId) {
    NodeEntry& n = node(nodeId);
    rel_id_t cur = n.firstRelId;
    while (cur != GRAPH_NONE) {
        RelEntry& r = rel(cur);
        rel_id_t next = (r.firstNodeId == nodeId) ? r.firstNextRelId : r.secondNextRelId;
        prop_id_t p = r.payload;
        while (p != GRAPH_NONE) {
            prop_id_t pnext = props_.ptr[p].nextPropId;
            props_.ptr[p].inUse = false;
            freeProps_.push_back(p);
            p = pnext;
        }
        r.payload = GRAPH_NONE;
        cur = next;
    }
    prop_id_t p = n.payload;
    while (p != GRAPH_NONE) {
        prop_id_t pnext = props_.ptr[p].nextPropId;
        props_.ptr[p].inUse = false;
        freeProps_.push_back(p);
        p = pnext;
    }
    n.payload = GRAPH_NONE;
    graph_.removeNode(nodeId);
}
rel_id_t PropertyGraph::addRelationship(node_id_t from, node_id_t to, uint32_t typeId) {
   return graph_.addRelationship(from, to, typeId, /*initPayload=*/GRAPH_NONE);
}
void PropertyGraph::removeRelationship(rel_id_t relId) {
   prop_id_t p = rel(relId).payload;
   while (p != GRAPH_NONE) {
      prop_id_t next = props_.ptr[p].nextPropId;
      props_.ptr[p].inUse = false;
      freeProps_.push_back(p);
      p = next;
   }
   graph_.removeRelationship(relId);
}
prop_id_t PropertyGraph::allocProp() {
    if (!freeProps_.empty()) {
        prop_id_t id = freeProps_.back();
        freeProps_.pop_back();
        return id;
    }
    assert(propMark_ < propCap_ && "prop capacity exceeded");
    return propMark_++;
}
prop_id_t PropertyGraph::addPropertyToChain(prop_id_t& chainHead,
                                            uint32_t key, uint32_t type, uint32_t value) {
    prop_id_t id = allocProp();
    PropRecord& p = props_.ptr[id];
    p.inUse      = true;
    p.key        = key;
    p.type       = type;
    p.value      = value;
    p.prevPropId = GRAPH_NONE;
    p.nextPropId = chainHead;
    if (chainHead != GRAPH_NONE)
        props_.ptr[chainHead].prevPropId = id;
    chainHead = id;
    return id;
}
prop_id_t PropertyGraph::addNodeProperty(node_id_t nodeId, uint32_t key, uint32_t type, uint32_t value) {
    return addPropertyToChain(node(nodeId).payload, key, type, value);
}
prop_id_t PropertyGraph::addRelProperty(rel_id_t relId, uint32_t key, uint32_t type, uint32_t value) {
    return addPropertyToChain(rel(relId).payload, key, type, value);
}
void PropertyGraph::setPropertyValue(prop_id_t id, uint32_t value) {
    props_.ptr[id].value = value;
}
void PropertyGraph::removeProperty(prop_id_t id, prop_id_t& chainHead) {
    PropRecord& p = props_.ptr[id];
    assert(p.inUse);
    if (p.prevPropId != GRAPH_NONE)
        props_.ptr[p.prevPropId].nextPropId = p.nextPropId;
    else
        chainHead = p.nextPropId;
    if (p.nextPropId != GRAPH_NONE)
        props_.ptr[p.nextPropId].prevPropId = p.prevPropId;
    p.inUse = false;
    freeProps_.push_back(id);
}
PropertyGraph* PropertyGraph::create(int32_t nodeCapacity, int32_t relCapacity, int32_t propCapacity) {
    return new PropertyGraph(nodeCapacity, relCapacity, propCapacity);
}
void PropertyGraph::destroy(PropertyGraph* g) {
   delete g;
}
} // namespace lingodb::runtime

