#include "gengodb/runtime/GraphData.h"

#include "gengodb/catalog/GraphCatalogEntry.h"

namespace lingodb::runtime {

std::unique_ptr<Neo4JGraph> GraphData::serialize(const PropertyGraph& pg) {
   auto neo = std::make_unique<Neo4JGraph>(
      static_cast<size_t>(pg.nodeHighWater()),
      static_cast<size_t>(pg.relHighWater()),
      static_cast<size_t>(pg.propHighWater()));

   const auto* pgNodes = reinterpret_cast<const PropertyGraph::NodeEntry*>(pg.nodeStorePtr());
   for (size_t i = 0; i < neo->nNodes; i++) {
      const auto& src = pgNodes[i];
      auto& dst       = neo->nodes.ptr[i];
      dst.inUse       = src.inUse;
      dst.firstRelId  = src.firstRelId;
      dst.firstPropId = static_cast<uint32_t>(src.payload);
      std::memcpy(dst.labels, src.labels, 5);
      dst.extra = src.extra;
   }

   const auto* pgRels = reinterpret_cast<const PropertyGraph::RelEntry*>(pg.relStorePtr());
   for (size_t i = 0; i < neo->nRels; i++) {
      const auto& src        = pgRels[i];
      auto& dst              = neo->rels.ptr[i];
      dst.inUse              = src.inUse;
      dst.firstNodeId        = src.firstNodeId;
      dst.secondNodeId       = src.secondNodeId;
      dst.typeId             = src.typeId;
      dst.firstPrevRelId     = src.firstPrevRelId;
      dst.firstNextRelId     = src.firstNextRelId;
      dst.secondPrevRelId    = src.secondPrevRelId;
      dst.secondNextRelId    = src.secondNextRelId;
      dst.firstPropId        = static_cast<uint32_t>(src.payload);
      dst.firstInChainMarker = src.firstInChainMarker;
   }

   const auto* pgProps = reinterpret_cast<const PropertyGraph::PropRecord*>(pg.propStorePtr());
   for (size_t i = 0; i < neo->nProps; i++) {
      const auto& src  = pgProps[i];
      auto& dst        = neo->props.ptr[i];
      dst.inUse        = src.inUse;
      dst.nextPropId   = src.nextPropId;
      dst.prevPropId   = src.prevPropId;
      dst.key          = src.key;
      dst.type         = src.type;
      dst.value        = src.value;
   }

   return neo;
}

void GraphData::deserialize(PropertyGraph& pg, const Neo4JGraph& g) {
    pg.clear();
    for (size_t i = 0; i < g.nNodes; i++) {
        const auto& src  = g.nodes.ptr[i];
        auto& dst        = pg.newNode();
        dst.firstRelId   = src.firstRelId;
        dst.inUse        = src.inUse;
        std::memcpy(dst.labels, src.labels, 5);
        dst.extra        = src.extra;
        dst.payload      = static_cast<prop_id_t>(src.firstPropId);
    }
    for (size_t i = 0; i < g.nRels; i++) {
        const auto& src        = g.rels.ptr[i];
        auto& dst              = pg.newRel();
        dst.firstNodeId        = src.firstNodeId;
        dst.secondNodeId       = src.secondNodeId;
        dst.typeId             = src.typeId;
        dst.firstPrevRelId     = src.firstPrevRelId;
        dst.firstNextRelId     = src.firstNextRelId;
        dst.secondPrevRelId    = src.secondPrevRelId;
        dst.secondNextRelId    = src.secondNextRelId;
        dst.inUse              = src.inUse;
        dst.firstInChainMarker = src.firstInChainMarker;
        dst.payload            = static_cast<prop_id_t>(src.firstPropId);
    }
    for (size_t i = 0; i < g.nProps; i++) {
        const auto& src  = g.props.ptr[i];
        auto& dst        = pg.newProp();
        dst.nextPropId   = src.nextPropId;
        dst.prevPropId   = src.prevPropId;
        dst.key          = src.key;
        dst.type         = src.type;
        dst.value        = src.value;
        dst.inUse        = src.inUse;
    }
}

uint8_t* GraphData::allocAndPopulateBuiltinGraph(int32_t builtin) {
    switch (builtin) {
        case 1: {
            auto g = PageRankGraph::create(16, 256);
            for (int i = 0; i < 5; i++) {
                g->addNode();
            }
            g->addRelationship(0, 1);
            g->addRelationship(1, 2);
            g->addRelationship(2, 4);
            g->addRelationship(3, 4);
            g->addRelationship(4, 1);
            g->addRelationship(0, 3);
            g->registerGraph();
            return reinterpret_cast<uint8_t*>(g);
        } break;
        case 2: {
            auto g = PropertyGraph::create(16, 256, 256);
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
            g->registerGraph();
            return reinterpret_cast<uint8_t*>(g);
        } break;
        default: {
            auto g = SimpleGraph::create(16, 256);
            for (int i = 0; i < 6; i++) {
                g->addNode();
            }
            g->addRelationship(0, 2);
            g->addRelationship(1, 0);
            g->addRelationship(1, 2);
            g->addRelationship(1, 4);
            g->addRelationship(2, 4);
            g->addRelationship(2, 3);
            g->setRelValue(0, 4242);
            g->setRelValue(1, 111);
            g->setRelValue(2, 222);
            g->setRelValue(3, 333);
            g->setRelValue(4, 444);
            g->setRelValue(5, 555);
            g->setNodeValue(0, 42);
            g->setNodeValue(1, 11);
            g->setNodeValue(2, 22);
            g->setNodeValue(3, 33);
            g->setNodeValue(4, 44);
            g->setNodeValue(5, 55);
            g->registerGraph();
            return reinterpret_cast<uint8_t*>(g);
        }
    };
}
PropertyGraph* GraphData::allocPropertyGraphState(size_t nodeBufLen, size_t relBufLen, size_t propBufLen) {
    auto g = PropertyGraph::create(nodeBufLen, relBufLen, propBufLen);
    g->registerGraph();
    return g;
}
SimpleGraph* GraphData::allocSimpleGraphState(size_t nodeBufLen, size_t relBufLen) {
    auto g = SimpleGraph::create(nodeBufLen, relBufLen);
    g->registerGraph();
    return g;
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
        pgraph.registerGraph();
        return &pgraph;
    } else {
        // TODO Load local file (file://) or download graph from the semantic web (http://)
        throw std::runtime_error("could not find graph");
    }
}

} // namespace lingodb::runtime