#include "gengodb/runtime/Graph.h"

#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {

template<class GraphT>
struct GraphStorageHelper {
    GraphStorageHelper(const uint8_t* graph)
        : graph_(convertType(graph)) {}
    inline size_t nodeCount() const {
        return static_cast<size_t>(graph_->nodeHighWater()) - graph_->freeNodes(); 
    }
    inline size_t relCount() const {
        return static_cast<size_t>(graph_->relHighWater()) - graph_->freeRels(); 
    }
    inline size_t propCount() const {
        return nodeCount() + relCount();
    }
    inline node_id_t nodeId(uint8_t* node) const {
        return (node - graph_->nodeStorePtr()) / sizeof(typename GraphT::Base::NodeEntry);
    }
    inline rel_id_t relId(uint8_t* rel) const {
        return (rel - graph_->relStorePtr()) / sizeof(typename GraphT::Base::RelEntry);
    }
    inline uint8_t* getRelationshipLListHeadOf(uint8_t* ref) {
        auto node = reinterpret_cast<GraphT::Base::NodeEntry*>(ref);
        return graph_->relStorePtr() + node->firstRelId * sizeof(typename GraphT::Base::RelEntry);
    }
    const GraphT* getStorage() const { return graph_; }
    inline uint8_t* nodeStorePtr() const { return graph_->nodeStorePtr(); }
    inline uint8_t* relStorePtr() const { return graph_->relStorePtr(); }
    inline int32_t nodeHighWater() const { return graph_->nodeHighWater(); }
    inline int32_t relHighWater() const { return graph_->relHighWater(); }
private:
    const GraphT* graph_;
    static inline const GraphT* convertType(const uint8_t* graph) {
        throw std::runtime_error("unsupported builtin graph");
    }
}; // GraphStorageHelper
template<>
inline const SimpleGraph* GraphStorageHelper<SimpleGraph>::convertType(const uint8_t* sgraph) {
    assert(BuiltinGraph::type(sgraph) == BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH);
    return reinterpret_cast<const SimpleGraph*>(sgraph);
}
template<>
inline const PropertyGraph* GraphStorageHelper<PropertyGraph>::convertType(const uint8_t* pgraph) {
    assert(BuiltinGraph::type(pgraph) == BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH);
    return reinterpret_cast<const PropertyGraph*>(pgraph);
}
template<>
inline const AlignmentGraph_8x3_8x1* GraphStorageHelper<AlignmentGraph_8x3_8x1>::convertType(const uint8_t* graph) {
    assert(BuiltinGraph::type(graph) == BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH);
    return reinterpret_cast<const AlignmentGraph_8x3_8x1*>(graph);
}
void GraphStorage::add(const uint8_t* start, size_t len, const uint8_t* graph) {
    mem_.push_back(std::make_tuple(start, len, graph));
}
void GraphStorage::remove(const uint8_t* graph) {
    for (auto& tup : mem_) {
        if (std::get<2>(tup) != graph)
            continue;
        std::get<2>(tup) = nullptr;
    }
}
size_t GraphStorage::nodeCount(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).nodeCount();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).nodeCount();
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).nodeCount();
        default: assert(false && "should not happen");
    }
}
size_t GraphStorage::relCount(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).relCount();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).relCount();
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).relCount();
        default: assert(false && "should not happen");
    }
}
size_t GraphStorage::propCount(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).propCount();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            auto pgraph = GraphStorageHelper<PropertyGraph>(graph).getStorage();
            return static_cast<size_t>(pgraph->propHighWater()) - pgraph->freeProps();
        }
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).propCount();
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::nodeStorePtr(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).nodeStorePtr();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).nodeStorePtr();
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).nodeStorePtr();
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::relStorePtr(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).relStorePtr();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).relStorePtr();
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).relStorePtr();
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::propStorePtr(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).getStorage()->propStorePtr();
        default: assert(false && "should not happen");
    }
}
int32_t GraphStorage::nodeHighWater(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).nodeHighWater();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).nodeHighWater();
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).nodeHighWater();
        default: assert(false && "should not happen");
    }
}
int32_t GraphStorage::relHighWater(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).relHighWater();
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).relHighWater();
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).relHighWater();
        default: assert(false && "should not happen");
    }
}
int32_t GraphStorage::propHighWater(const uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).getStorage()->propHighWater();
        default: assert(false && "should not happen");
    }
}
class GraphTableIterator : public BufferIterator {
private:
    size_t entrySize;
    size_t entryCount;
    uint8_t* ptr;
    bool valid;
public:
    GraphTableIterator(size_t entrySize, size_t entryCount, uint8_t* ptr) 
        : entrySize(entrySize), entryCount(entryCount), ptr(ptr), valid(true) {}
    bool isValid() override { return valid; }
    void next() override { valid = false; }
    Buffer getCurrentBuffer() override { return Buffer{entrySize * entryCount, ptr}; };
    void iterateEfficient(bool parallel, void (*forEachChunk)(Buffer, void*), void* contextPtr) override {
        // TODO No parallelism in graph iterators yet...
        auto buffer = getCurrentBuffer();
        forEachChunk(buffer, contextPtr);
    }
}; // GraphTableIterator
BufferIterator* GraphStorage::createNodeIterator(uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH: {
            GraphStorageHelper<SimpleGraph> helper(graph);
            return new GraphTableIterator(
                sizeof(SimpleGraph::NodeEntry), helper.nodeHighWater(), helper.nodeStorePtr());
        }
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            GraphStorageHelper<PropertyGraph> helper(graph);
            return new GraphTableIterator(
                sizeof(PropertyGraph::NodeEntry), helper.nodeHighWater(), helper.nodeStorePtr());
        }
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH: {
            GraphStorageHelper<AlignmentGraph_8x3_8x1> helper(graph);
            return new GraphTableIterator(
                sizeof(AlignmentGraph_8x3_8x1::NodeEntry), helper.nodeHighWater(), helper.nodeStorePtr());
        }
        default: assert(false && "should not happen");
    }
}
BufferIterator* GraphStorage::createRelIterator(uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH: {
            GraphStorageHelper<SimpleGraph> helper(graph);
            return new GraphTableIterator(
                sizeof(SimpleGraph::RelEntry), helper.relHighWater(), helper.relStorePtr());
        }
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            GraphStorageHelper<PropertyGraph> helper(graph);
            return new GraphTableIterator(
                sizeof(PropertyGraph::RelEntry), helper.relHighWater(), helper.relStorePtr());
        }
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH: {
            GraphStorageHelper<AlignmentGraph_8x3_8x1> helper(graph);
            return new GraphTableIterator(
                sizeof(AlignmentGraph_8x3_8x1::RelEntry), helper.relHighWater(), helper.relStorePtr());
        }
        default: assert(false && "should not happen");
    }
}
BufferIterator* GraphStorage::createPropIterator(uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            GraphStorageHelper<PropertyGraph> helper(graph);
            return new GraphTableIterator(sizeof(PropertyGraph::PropRecord), helper.getStorage()->propHighWater(), 
                helper.getStorage()->propStorePtr());
        }
        default: assert(false && "should not happen");
    }
}
node_id_t GraphStorage::nodeId(uint8_t* node) {
    auto graph = lookupGraph(node);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).nodeId(node);
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).nodeId(node);
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).nodeId(node);
        default: assert(false && "should not happen");
    }
}
rel_id_t GraphStorage::relId(uint8_t* rel) {
    auto graph = lookupGraph(rel);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).relId(rel);
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).relId(rel);
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).relId(rel);
        default: assert(false && "should not happen");
    }
}
prop_id_t GraphStorage::propId(uint8_t* prop) {
    auto graph = lookupGraph(prop);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            GraphStorageHelper<PropertyGraph> helper(graph);
            auto pgraph = helper.getStorage();
            return (prop - pgraph->propStorePtr()) / sizeof(PropertyGraph::PropRecord);
        }
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::getRelationshipLListHeadOf(uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_SIMPLE_GRAPH:
            return GraphStorageHelper<SimpleGraph>(graph).getRelationshipLListHeadOf(ref);
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH:
            return GraphStorageHelper<PropertyGraph>(graph).getRelationshipLListHeadOf(ref);
        case BuiltinGraph::Type::BUILTIN_ALIGN_8X3_8X1_GRAPH:
            return GraphStorageHelper<AlignmentGraph_8x3_8x1>(graph).getRelationshipLListHeadOf(ref);
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::getNodePropertyLListHeadOf(uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            GraphStorageHelper<PropertyGraph> helper(graph);
            auto pgraph = helper.getStorage();
            auto node = reinterpret_cast<PropertyGraph::NodeEntry*>(ref);
            return pgraph->propStorePtr() + node->payload * sizeof(PropertyGraph::PropRecord);
        }
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::getRelPropertyLListHeadOf(uint8_t* ref) {
    auto graph = lookupGraph(ref);
    switch(BuiltinGraph::typeId(graph)) {
        case BuiltinGraph::Type::BUILTIN_PROPERTY_GRAPH: {
            GraphStorageHelper<PropertyGraph> helper(graph);
            auto pgraph = helper.getStorage();
            auto rel = reinterpret_cast<PropertyGraph::RelEntry*>(ref);
            return pgraph->propStorePtr() + rel->payload * sizeof(PropertyGraph::PropRecord);
        }
        default: assert(false && "should not happen");
    }
}
uint8_t* GraphStorage::graphPtr(const uint8_t* ref) { 
    return const_cast<uint8_t*>(lookupGraph(ref)); 
}
template<>
const PropertyGraph* GraphStorage::lookupGraph<PropertyGraph>(const uint8_t* ref) {
    const uint8_t* ptr = lookupGraph(ref);
    assert(BuiltinGraph::type(ptr) == BuiltinGraph::BUILTIN_PROPERTY_GRAPH);
    return reinterpret_cast<const PropertyGraph*>(ptr);
}
template<>
const SimpleGraph* GraphStorage::lookupGraph<SimpleGraph>(const uint8_t* ref) {
    const uint8_t* ptr = lookupGraph(ref);
    assert(BuiltinGraph::type(ptr) == BuiltinGraph::BUILTIN_SIMPLE_GRAPH);
    return reinterpret_cast<const SimpleGraph*>(ptr);
}
template<>
const AlignmentGraph_8x3_8x1* GraphStorage::lookupGraph<AlignmentGraph_8x3_8x1>(const uint8_t* ref) {
    const uint8_t* ptr = lookupGraph(ref);
    assert(BuiltinGraph::type(ptr) == BuiltinGraph::BUILTIN_ALIGN_8X3_8X1_GRAPH);
    return reinterpret_cast<const AlignmentGraph_8x3_8x1*>(ptr);
}
std::vector<std::tuple<const uint8_t*, size_t, const uint8_t*>> GraphStorage::mem_;

} // namespace lingodb::runtime