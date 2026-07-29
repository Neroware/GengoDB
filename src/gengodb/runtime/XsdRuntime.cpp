#include "gengodb/runtime/XsdRuntime.h"

#include "gengodb/semantics/RdfGraph.h"

#include <rdf4cpp/Literal.hpp>
#include <rdf4cpp/TriBool.hpp>

#include <optional>

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
rdf4cpp::Literal literalFromLexicalType(VarLen32 lexicalForm, int32_t xsdTypeRaw) {
    auto xsdType = gengodb::semantics::xsd::from_int32(xsdTypeRaw);
    if (!xsdType.has_value()) return rdf4cpp::Literal::make_null();
    auto datatype = extra_namespaces().XSD + gengodb::semantics::xsd::to_string(xsdType.value());
    return rdf4cpp::Literal::make_typed(lexicalForm.str(), datatype);
}
constexpr int32_t kUnspecified = 0;
VarLen32 emptyVarLen32() { return VarLen32{}; }
std::optional<rdf4cpp::Literal> literalOfRef(PropertyGraph::NodeEntry* ref) {
    const RdfGraph* graph = owningRdfGraph(ref);
    int32_t id = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(ref));
    if (graph->getNodeType(id) != gengodb::semantics::RDFNodeType::Literal) return std::nullopt;
    return graph->getLiteral(id);
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
    auto lhsLiteral = literalOfRef(lhs);
    if (!lhsLiteral.has_value()) return -1;
    auto rhs = literalFromLexicalType(rhsLexicalForm, rhsXsdType);
    return triBoolToInt8(applyPredicate(*lhsLiteral, rhs, predicate));
}
VarLen32 XsdRuntime::literalLexicalOfRef(PropertyGraph::NodeEntry* ref) {
    auto lit = literalOfRef(ref);
    if (!lit.has_value()) return emptyVarLen32();
    return VarLen32::fromString(std::string(lit->lexical_form()));
}
int32_t XsdRuntime::literalTypeOfRef(PropertyGraph::NodeEntry* ref) {
    auto lit = literalOfRef(ref);
    if (!lit.has_value()) return kUnspecified;
    return static_cast<int32_t>(gengodb::semantics::xsd::from_iri(lit->datatype()));
}

namespace {
rdf4cpp::Literal applyArith(const rdf4cpp::Literal& lhs, const rdf4cpp::Literal& rhs, int32_t predicate) {
    try {
        switch (predicate) {
            case 0: return lhs + rhs;
            case 1: return lhs - rhs;
            case 2: return lhs * rhs;
            case 3: return lhs / rhs;
            default: return rdf4cpp::Literal::make_null();
        }
    } catch (...) {
        return rdf4cpp::Literal::make_null();
    }
}
} // namespace

VarLen32 XsdRuntime::arithLexical(VarLen32 lhsLexicalForm, int32_t lhsXsdType, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate) {
    if (lhsXsdType == kUnspecified || rhsXsdType == kUnspecified) return emptyVarLen32();
    auto result = applyArith(literalFromLexicalType(lhsLexicalForm, lhsXsdType), literalFromLexicalType(rhsLexicalForm, rhsXsdType), predicate);
    if (result.null()) return emptyVarLen32();
    return VarLen32::fromString(std::string(result.lexical_form()));
}
int32_t XsdRuntime::arithType(VarLen32 lhsLexicalForm, int32_t lhsXsdType, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate) {
    if (lhsXsdType == kUnspecified || rhsXsdType == kUnspecified) return kUnspecified;
    auto result = applyArith(literalFromLexicalType(lhsLexicalForm, lhsXsdType), literalFromLexicalType(rhsLexicalForm, rhsXsdType), predicate);
    if (result.null()) return kUnspecified;
    return static_cast<int32_t>(gengodb::semantics::xsd::from_iri(result.datatype()));
}
VarLen32 XsdRuntime::negateLexical(VarLen32 lexicalForm, int32_t xsdType) {
    if (xsdType == kUnspecified) return emptyVarLen32();
    rdf4cpp::Literal result;
    try {
        result = -literalFromLexicalType(lexicalForm, xsdType);
    } catch (...) {
        return emptyVarLen32();
    }
    if (result.null()) return emptyVarLen32();
    return VarLen32::fromString(std::string(result.lexical_form()));
}
int32_t XsdRuntime::negateType(VarLen32 lexicalForm, int32_t xsdType) {
    if (xsdType == kUnspecified) return kUnspecified;
    rdf4cpp::Literal result;
    try {
        result = -literalFromLexicalType(lexicalForm, xsdType);
    } catch (...) {
        return kUnspecified;
    }
    if (result.null()) return kUnspecified;
    return static_cast<int32_t>(gengodb::semantics::xsd::from_iri(result.datatype()));
}
int8_t XsdRuntime::compareDyn(VarLen32 lhsLexicalForm, int32_t lhsXsdType, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate) {
    if (lhsXsdType == kUnspecified || rhsXsdType == kUnspecified) return -1;
    return triBoolToInt8(applyPredicate(literalFromLexicalType(lhsLexicalForm, lhsXsdType), literalFromLexicalType(rhsLexicalForm, rhsXsdType), predicate));
}
