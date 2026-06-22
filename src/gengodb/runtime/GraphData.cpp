#include "gengodb/runtime/GraphData.h"

#include "gengodb/catalog/GraphCatalogEntry.h"
#include "gengodb/semantics/XSDType.h"
#include "gengodb/semantics/RdfGraph.h"

namespace lingodb::runtime {
using namespace gengodb::semantics;

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
            auto g = AlignmentGraph_8x3_8x1::create(16, 256);
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
            g->addNodeProperty(0, 0, static_cast<uint32_t>(xsd::Type::Int), 424);
            g->addNodeProperty(1, 11, static_cast<uint32_t>(xsd::Type::Int), 100);
            g->addNodeProperty(1, 11, static_cast<uint32_t>(xsd::Type::Short), 101);
            g->addNodeProperty(1, 11, static_cast<uint32_t>(xsd::Type::Boolean), 0xffffffff);
            g->addNodeProperty(2, 22, static_cast<uint32_t>(xsd::Type::Int), 200);
            g->addNodeProperty(3, 33, static_cast<uint32_t>(xsd::Type::Int), 300);
            g->addNodeProperty(5, 55, static_cast<uint32_t>(xsd::Type::Int), 501);
            g->addNodeProperty(5, 55, static_cast<uint32_t>(xsd::Type::Int), 502);
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
        if (graph->getGraphIri().identifier() != iri.str()) {
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

VarLen32 PropertyData::lookupStr(PropertyGraph::PropRecord* prop) {
    if (prop->type != static_cast<uint32_t>(xsd::Type::String)) {
        return VarLen32::fromString("");
    }
    PropertyGraph* pgraph = reinterpret_cast<PropertyGraph*>(
        GraphStorage::graphPtr(reinterpret_cast<uint8_t*>(prop)));
    auto [data, len] = pgraph->getPropData().get_blob<xsd::Type::String>(prop->value);
    return VarLen32::fromString(std::string(reinterpret_cast<const char*>(data), len));
}

namespace {

struct XSDPropertyStringifier {
    xsd::Type type;
    PropertyGraph* pgraph;
    XSDPropertyStringifier(xsd::Type type, PropertyGraph* pgraph) : type(type), pgraph(pgraph) {}
    ~XSDPropertyStringifier() = default;
    template<typename T>
    inline VarLen32 from_inlined(int32_t value) const {
        static_assert(sizeof(T) <= sizeof(int32_t));
        static_assert(std::is_trivially_copyable_v<T>);
        using Raw = std::conditional_t<sizeof(T) == 1, uint8_t,
                    std::conditional_t<sizeof(T) == 2, uint16_t,
                    std::conditional_t<sizeof(T) == 4, uint32_t, void>>>;
        Raw raw = static_cast<Raw>(static_cast<uint32_t>(value));
        T typed_value = std::bit_cast<T>(raw);
        return VarLen32::fromString("'" + std::to_string(typed_value) + "'^^xsd:" + xsd::to_string(type));
    }
    inline VarLen32 from_bool(int32_t value) const {
        using Raw = std::conditional_t<sizeof(bool) == 1, uint8_t,
                    std::conditional_t<sizeof(bool) == 2, uint16_t,
                    std::conditional_t<sizeof(bool) == 4, uint32_t, void>>>;
        Raw raw = static_cast<Raw>(static_cast<uint32_t>(value));
        bool typed_value = std::bit_cast<bool>(raw);
        return VarLen32::fromString(std::string(typed_value ? "'true'" : "'false'") + "^^xsd:" + xsd::to_string(type));
    }
    inline VarLen32 from_str(int32_t value) const {
        auto [ptr, len] = pgraph->getPropData().get_blob<xsd::Type::String>(value);
        return VarLen32::fromString(std::string(reinterpret_cast<const char*>(ptr), len));
    }
    inline VarLen32 from_iri(int32_t value) {
        IRI iri = pgraph->getMetadata().id(value);
        return VarLen32::fromString("<" + static_cast<std::string>(iri) + ">");
    }
};

} // namespace

VarLen32 XSDString::fromProp(PropertyGraph::PropRecord* prop) {
    if (!xsd::from_int32(static_cast<int32_t>(prop->type)).has_value()) {
        assert(false && "should not happen");
    }
    auto xsdtype = xsd::from_int32(static_cast<int32_t>(prop->type)).value();
    XSDPropertyStringifier xsdStr(xsdtype, reinterpret_cast<PropertyGraph*>(
        GraphStorage::graphPtr(reinterpret_cast<uint8_t*>(prop))));
    switch(xsdtype) {
        case xsd::Type::Boolean:        return xsdStr.from_bool(prop->value);
        case xsd::Type::Float:          return xsdStr.from_inlined<float>(prop->value);
        case xsd::Type::Int:            return xsdStr.from_inlined<uint32_t>(prop->value);
        case xsd::Type::Short:          return xsdStr.from_inlined<int16_t>(prop->value);
        case xsd::Type::Byte:           return xsdStr.from_inlined<int8_t>(prop->value);
        case xsd::Type::UnsignedInt:    return xsdStr.from_inlined<int32_t>(prop->value);
        case xsd::Type::UnsignedShort:  return xsdStr.from_inlined<uint16_t>(prop->value);
        case xsd::Type::UnsignedByte:   return xsdStr.from_inlined<uint8_t>(prop->value);
        default:                        return VarLen32::fromString("<<UNKNOWN TYPE>>");
    }
}
VarLen32 XSDString::fromNode(PropertyGraph::NodeEntry* node) {
    if (node->payload < 0) {
        return VarLen32::fromString("<<UNKNOWN NODE>>");
    }
    PropertyGraph* pgraph = reinterpret_cast<PropertyGraph*>(
        GraphStorage::graphPtr(reinterpret_cast<uint8_t*>(node)));
    return fromProp(&pgraph->prop(node->payload));
}
VarLen32 XSDString::fromRel(PropertyGraph::RelEntry* rel) {
    return VarLen32::fromString("<<UNKNOWN RELATION>>");
}

} // namespace lingodb::runtime