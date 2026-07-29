#include "gengodb/runtime/XsdRuntime.h"

#include "gengodb/semantics/RdfGraph.h"

#include <rdf4cpp/Literal.hpp>
#include <rdf4cpp/TriBool.hpp>

using namespace lingodb::runtime;
using gengodb::semantics::extra_namespaces;
using gengodb::semantics::RdfGraph;

namespace {
int8_t triBoolToInt8(rdf4cpp::TriBool b) {
    if (b == rdf4cpp::TriBool::Err) return -1;
    return b ? 1 : 0;
}
rdf4cpp::TriBool applyPredicate(const rdf4cpp::Literal& lhs, const rdf4cpp::Literal& rhs, int32_t predicate) {
    switch (predicate) {
        case 0: return lhs.eq(rhs);
        case 1: return lhs.ne(rhs);
        case 2: return lhs.lt(rhs);
        case 3: return lhs.le(rhs);
        case 4: return lhs.gt(rhs);
        case 5: return lhs.ge(rhs);
        default: return rdf4cpp::TriBool::Err;
    }
}
const RdfGraph* owningRdfGraph(PropertyGraph::NodeEntry* node) {
    PropertyGraph* pgraph = reinterpret_cast<PropertyGraph*>(
        GraphStorage::graphPtr(reinterpret_cast<uint8_t*>(node)));
    return pgraph->getMetadata().get_rdf();
}
} // namespace

int8_t XsdRuntime::compareNodeNode(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate) {
    const RdfGraph* lhsGraph = owningRdfGraph(lhs);
    const RdfGraph* rhsGraph = owningRdfGraph(rhs);
    int32_t lhsId = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(lhs));
    int32_t rhsId = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(rhs));
    if (lhsGraph->getNodeType(lhsId) != gengodb::semantics::RDFNodeType::Literal ||
        rhsGraph->getNodeType(rhsId) != gengodb::semantics::RDFNodeType::Literal) {
        return -1;
    }
    return triBoolToInt8(applyPredicate(lhsGraph->getLiteral(lhsId), rhsGraph->getLiteral(rhsId), predicate));
}

int8_t XsdRuntime::compareNodeLiteral(PropertyGraph::NodeEntry* lhs, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate) {
    const RdfGraph* lhsGraph = owningRdfGraph(lhs);
    int32_t lhsId = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(lhs));
    if (lhsGraph->getNodeType(lhsId) != gengodb::semantics::RDFNodeType::Literal) {
        return -1;
    }
    auto xsdType = gengodb::semantics::xsd::from_int32(rhsXsdType);
    if (!xsdType.has_value()) {
        return -1;
    }
    auto datatype = extra_namespaces().XSD + gengodb::semantics::xsd::to_string(xsdType.value());
    auto rhs = rdf4cpp::Literal::make_typed(rhsLexicalForm.str(), datatype);
    return triBoolToInt8(applyPredicate(lhsGraph->getLiteral(lhsId), rhs, predicate));
}
