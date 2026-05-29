#include "gengodb/runtime/GraphData.h"

#include "gengodb/catalog/GraphCatalogEntry.h"

namespace lingodb::runtime {

uint8_t* GraphData::allocAndPopulateBuiltinGraph(int32_t builtin) {
    auto* context = getCurrentExecutionContext();
    assert(context);
    switch (builtin) {
        case 1: {
            auto g = SimpleGraph::create(16, 256);
            context->registerState({g, [](void* p) { delete (reinterpret_cast<SimpleGraph*>(p)); }});
            for (int i = 0; i < 5; i++) {
                g->addNode();
            }
            g->addRelationship(0, 1);
            g->addRelationship(1, 2);
            g->addRelationship(2, 4);
            g->addRelationship(3, 4);
            g->addRelationship(4, 1);
            g->addRelationship(0, 3);
            return reinterpret_cast<uint8_t*>(g);
        } break;
        case 2: {
            auto g = PropertyGraph::create(16, 256, 256);
            context->registerState({g, [](void* p) { delete (reinterpret_cast<PropertyGraph*>(p)); }});
            for (int i = 0; i < 6; i++) {
                g->addNode();
            }
            g->addRelationship(0, 2, 0);
            g->addRelationship(1, 0, 0);
            g->addRelationship(1, 2, 0);
            g->addRelationship(1, 4, 0);
            g->addRelationship(2, 4, 0);
            g->addRelationship(2, 3, 0);
            g->addNodeProperty(0, 0, 0, 424);
            g->addNodeProperty(1, 11, 11, 100);
            g->addNodeProperty(1, 11, 11, 101);
            g->addNodeProperty(1, 11, 11, 102);
            g->addNodeProperty(2, 22, 22, 200);
            g->addNodeProperty(3, 33, 33, 300);
            g->addNodeProperty(5, 55, 55, 501);
            g->addNodeProperty(5, 55, 55, 502);
            g->addRelProperty(0, 0, 0, 4242);
            g->addRelProperty(1, 11, 11, 1000);
            g->addRelProperty(2, 22, 22, 2000);
            g->addRelProperty(3, 33, 33, 3000);
            return reinterpret_cast<uint8_t*>(g);
        } break;
        default: {
            auto g = SimpleGraph::create(16, 256);
            context->registerState({g, [](void* p) { delete (reinterpret_cast<SimpleGraph*>(p)); }});
            for (int i = 0; i < 6; i++) {
                g->addNode();
            }
            g->addRelationship(0, 2);
            g->addRelationship(1, 0);
            g->addRelationship(1, 2);
            g->addRelationship(1, 4);
            g->addRelationship(2, 4);
            g->addRelationship(2, 3);
            g->setRelValue<uint64_t>(0, 32);
            g->setRelValue<uint64_t>(1, 111);
            g->setRelValue<uint64_t>(2, 222);
            g->setRelValue<uint64_t>(3, 333);
            g->setRelValue<uint64_t>(4, 444);
            g->setRelValue<uint64_t>(5, 555);
            g->setNodeValue<uint64_t>(0, 42);
            g->setNodeValue<uint64_t>(1, 11);
            g->setNodeValue<uint64_t>(2, 22);
            g->setNodeValue<uint64_t>(3, 33);
            g->setNodeValue<uint64_t>(4, 44);
            g->setNodeValue<uint64_t>(5, 55);
            return reinterpret_cast<uint8_t*>(g);
        }
    };
}
PropertyGraph* GraphData::allocPropertyGraphState(size_t nodeBufLen, size_t relBufLen, size_t propBufLen) {
    auto* context = getCurrentExecutionContext();
    assert(context);
    auto ptr = static_cast<void*>(PropertyGraph::create(nodeBufLen, relBufLen, propBufLen));
    context->registerState({ptr, [](void* p) { delete (reinterpret_cast<PropertyGraph*>(p)); }});
    return static_cast<PropertyGraph*>(ptr);
}
SimpleGraph* GraphData::allocSimpleGraphState(size_t nodeBufLen, size_t relBufLen) {
    auto* context = getCurrentExecutionContext();
    assert(context);
    auto ptr = static_cast<void*>(SimpleGraph::create(nodeBufLen, relBufLen));
    context->registerState({ptr, [](void* p) { delete (reinterpret_cast<SimpleGraph*>(p)); }});
    return static_cast<SimpleGraph*>(ptr);
}
void GraphData::createGraph(lingodb::runtime::VarLen32 meta) {
    // TODO implement
    assert(false && "not implemented");
}
PropertyGraph* GraphData::getGraph(lingodb::runtime::VarLen32 name, lingodb::runtime::VarLen32 iri) {
    lingodb::runtime::ExecutionContext* executionContext = lingodb::runtime::getCurrentExecutionContext();
    auto& session = executionContext->getSession();
    if (auto maybeGraph = session.getCatalog()->getTypedEntry<gengodb::catalog::RDFGraphCatalogEntry>(name)) {
        auto graph = maybeGraph.value();
        if (graph->getIri().identifier() != iri.str()) {
            throw std::runtime_error("Found graph record but IRIs do not match!");
        }
        auto& pgraph = graph->getStorage();
        getCurrentExecutionContext()->registerState({&pgraph, [](void* ptr){ delete reinterpret_cast<PropertyGraph*>(ptr); }});
        return &pgraph;
    } else {
        // TODO Load local file (file://) or download graph from the semantic web (http://)
        throw std::runtime_error("could not find graph");
    }
}

} // namespace lingodb::runtime