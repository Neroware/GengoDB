#include "gengodb/runtime/Variant.h"

#include "gengodb/semantics/RdfGraph.h"

#include <rdf4cpp/IRI.hpp>
#include <rdf4cpp/Literal.hpp>
#include <rdf4cpp/TriBool.hpp>

#include <cstring>
#include <optional>
#include <string>
#include <string_view>

using namespace lingodb::runtime;
namespace xsd = gengodb::semantics::xsd;

namespace {

inline constexpr int8_t triBoolToInt8(rdf4cpp::TriBool b) {
    if (b == rdf4cpp::TriBool::Err) return -1;
    return b ? 1 : 0;
}
inline constexpr rdf4cpp::TriBool applyPredicate(const rdf4cpp::Literal& lhs, const rdf4cpp::Literal& rhs, int32_t predicate) {
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
inline rdf4cpp::Literal applyArith(const rdf4cpp::Literal& lhs, const rdf4cpp::Literal& rhs, int32_t predicate) {
    try {
        switch (predicate) {
            case 0: return lhs + rhs;
            case 1: return lhs - rhs;
            case 2: return lhs * rhs;
            case 3: return lhs / rhs;
            default: return rdf4cpp::Literal::make_null();
        }
    }
    catch (...) {
        return rdf4cpp::Literal::make_null();
    }
}
inline VarLen32 emptyVarLen32() { return VarLen32{}; }

constexpr std::string_view kXsdNamespace = "http://www.w3.org/2001/XMLSchema#";
inline rdf4cpp::IRI toDatatypeIri(xsd::Type t) {
    return rdf4cpp::IRI(std::string(kXsdNamespace) + xsd::to_string(t));
}

inline rdf4cpp::Literal getLiteralFromNumeric(uint8_t* ptr, int32_t tag) {
    auto t = xsd::from_int32(tag);
    if (!t.has_value()) return rdf4cpp::Literal::make_null();
    switch (*t) {
        case xsd::Type::Boolean: {
            bool v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Boolean>(v);
        }
        case xsd::Type::Byte: {
            int8_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Byte>(v);
        }
        case xsd::Type::Short: {
            int16_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Short>(v);
        }
        case xsd::Type::Int: {
            int32_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Int>(v);
        }
        case xsd::Type::Long: {
            int64_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Long>(v);
        }
        case xsd::Type::UnsignedByte: {
            uint8_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::UnsignedByte>(v);
        }
        case xsd::Type::UnsignedShort: {
            uint16_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::UnsignedShort>(v);
        }
        case xsd::Type::UnsignedInt: {
            uint32_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::UnsignedInt>(v);
        }
        case xsd::Type::UnsignedLong: {
            uint64_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::UnsignedLong>(v);
        }
        case xsd::Type::Float: {
            float v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Float>(v);
        }
        case xsd::Type::Double: {
            double v;
            std::memcpy(&v, ptr, sizeof(v));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Double>(v);
        }
        case xsd::Type::Date: {
            int64_t packed;
            std::memcpy(&packed, ptr, sizeof(packed));
            return rdf4cpp::Literal::make_typed_from_value<rdf4cpp::datatypes::xsd::Date>(
                gengodb::semantics::RdfDatatypeFixedHelper::unpackDate(packed));
        }
        default: return rdf4cpp::Literal::make_null();
    }
}
inline bool extractNumericByTag(const rdf4cpp::Literal& lit, int32_t tag, uint8_t* outPtr) {
    auto t = xsd::from_int32(tag);
    if (!t.has_value()) return false;
    switch (*t) {
        case xsd::Type::Boolean: {
            bool v = lit.template value<rdf4cpp::datatypes::xsd::Boolean>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Byte: {
            int8_t v = lit.template value<rdf4cpp::datatypes::xsd::Byte>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Short: {
            int16_t v = lit.template value<rdf4cpp::datatypes::xsd::Short>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Int: {
            int32_t v = lit.template value<rdf4cpp::datatypes::xsd::Int>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Long: {
            int64_t v = lit.template value<rdf4cpp::datatypes::xsd::Long>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::UnsignedByte: {
            uint8_t v = lit.template value<rdf4cpp::datatypes::xsd::UnsignedByte>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::UnsignedShort: {
            uint16_t v = lit.template value<rdf4cpp::datatypes::xsd::UnsignedShort>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::UnsignedInt: {
            uint32_t v = lit.template value<rdf4cpp::datatypes::xsd::UnsignedInt>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::UnsignedLong: {
            uint64_t v = lit.template value<rdf4cpp::datatypes::xsd::UnsignedLong>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Float: {
            float v = lit.template value<rdf4cpp::datatypes::xsd::Float>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Double: {
            double v = lit.template value<rdf4cpp::datatypes::xsd::Double>();
            std::memcpy(outPtr, &v, sizeof(v));
            return true;
        }
        case xsd::Type::Date: {
            int64_t packed = gengodb::semantics::RdfDatatypeFixedHelper::packDate(
                lit.template value<rdf4cpp::datatypes::xsd::Date>());
            std::memcpy(outPtr, &packed, sizeof(packed));
            return true;
        }
        default: return false;
    }
}

// ---- Direct PGraph storage access. No RdfGraph, no rdf4cpp. ----

inline PropertyGraph* propertyGraphOf(PropertyGraph::NodeEntry* ref) {
    return reinterpret_cast<PropertyGraph*>(
        GraphStorage::graphPtr(reinterpret_cast<uint8_t*>(ref)));
}

enum class StorageShape { Inline32, Int64, UInt64, Double, Blob };
struct StorageInfo {
    StorageShape shape;
    size_t width;
};
constexpr StorageInfo classifyStorage(xsd::Type t) {
    switch (t) {
        case xsd::Type::Boolean: return {StorageShape::Inline32, sizeof(bool)};
        case xsd::Type::Byte: return {StorageShape::Inline32, sizeof(int8_t)};
        case xsd::Type::Short: return {StorageShape::Inline32, sizeof(int16_t)};
        case xsd::Type::Int: return {StorageShape::Inline32, sizeof(int32_t)};
        case xsd::Type::UnsignedByte: return {StorageShape::Inline32, sizeof(uint8_t)};
        case xsd::Type::UnsignedShort: return {StorageShape::Inline32, sizeof(uint16_t)};
        case xsd::Type::UnsignedInt: return {StorageShape::Inline32, sizeof(uint32_t)};
        case xsd::Type::Float: return {StorageShape::Inline32, sizeof(float)};
        case xsd::Type::Long: return {StorageShape::Int64, sizeof(int64_t)};
        case xsd::Type::Date: return {StorageShape::Int64, sizeof(int64_t)}; // packed, see RdfDatatypeFixedHelper
        case xsd::Type::UnsignedLong: return {StorageShape::UInt64, sizeof(uint64_t)};
        case xsd::Type::Double: return {StorageShape::Double, sizeof(double)};
        default: return {StorageShape::Blob, 0};
    }
}

inline std::optional<rdf4cpp::Literal> reconstructBlobLiteral(PropertyGraph::NodeEntry* ref) {
    if (ref->payload < 0) return std::nullopt;
    PropertyGraph* pgraph = propertyGraphOf(ref);
    const auto& prop = pgraph->prop(ref->payload);
    auto t = xsd::from_int32(static_cast<int32_t>(prop.type));
    if (!t.has_value()) return std::nullopt;
    auto [ptr, len] = pgraph->getPropData().get_blob<xsd::Type::String>(static_cast<int32_t>(prop.value));
    const char* raw = reinterpret_cast<const char*>(ptr);
    const auto langLen = static_cast<uint8_t>(raw[0]);
    const std::string_view lang(raw + 1, langLen);
    const std::string_view lex(raw + 1 + langLen, len - 1 - langLen);
    if (!lang.empty()) return rdf4cpp::Literal::make_lang_tagged(lex, lang);
    return rdf4cpp::Literal::make_typed(lex, toDatatypeIri(*t));
}

inline std::optional<rdf4cpp::Literal> reconstructLiteral(int64_t payload, int32_t tag, PropertyGraph::NodeEntry* ref) {
    auto t = xsd::from_int32(tag);
    if (!t.has_value() || *t == xsd::Type::Unspecified) return std::nullopt;
    if (*t == xsd::Type::String) {
        VarLen32 sv;
        std::memcpy(&sv, ref, sizeof(sv));
        return rdf4cpp::Literal::make_typed(std::string_view(sv.data(), sv.getLen()), toDatatypeIri(xsd::Type::String));
    }
    if (classifyStorage(*t).shape == StorageShape::Blob) return reconstructBlobLiteral(ref);
    auto lit = getLiteralFromNumeric(reinterpret_cast<uint8_t*>(&payload), tag);
    if (lit.null()) return std::nullopt;
    return lit;
}

} // namespace

int32_t VariantRuntime::resolveRefTag(PropertyGraph::NodeEntry* ref) {
    if (ref->payload < 0) return xsd::to_int32(xsd::Type::RDFNode);
    PropertyGraph* pgraph = propertyGraphOf(ref);
    return static_cast<int32_t>(pgraph->prop(ref->payload).type);
}

int64_t VariantRuntime::resolveNodeRef(PropertyGraph::NodeEntry* ref) {
    PropertyGraph* pgraph = propertyGraphOf(ref);
    const int32_t localId = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(ref));
    return static_cast<int64_t>(pgraph->getMetadata().uid(localId));
}

void VariantRuntime::extractNumericLiteral(PropertyGraph::NodeEntry* ref, int32_t tag, uint8_t* outPtr) {
    if (ref->payload < 0) return;
    auto t = xsd::from_int32(tag);
    if (!t.has_value()) return;
    PropertyGraph* pgraph = propertyGraphOf(ref);
    const auto& prop = pgraph->prop(ref->payload);
    const auto info = classifyStorage(*t);
    switch (info.shape) {
        case StorageShape::Inline32:
            std::memcpy(outPtr, &prop.value, info.width);
            return;
        case StorageShape::Int64:
            std::memcpy(outPtr, pgraph->getPropData().get_i64_ptr(static_cast<int32_t>(prop.value)), info.width);
            return;
        case StorageShape::UInt64:
            std::memcpy(outPtr, pgraph->getPropData().get_ui64_ptr(static_cast<int32_t>(prop.value)), info.width);
            return;
        case StorageShape::Double:
            std::memcpy(outPtr, pgraph->getPropData().get_double_ptr(static_cast<int32_t>(prop.value)), info.width);
            return;
        case StorageShape::Blob:
            return; // tag isn't a fixed/inlined numeric shape
    }
}

VarLen32 VariantRuntime::extractBlobLiteral(PropertyGraph::NodeEntry* ref) {
    if (ref->payload < 0) return emptyVarLen32();
    PropertyGraph* pgraph = propertyGraphOf(ref);
    const auto& prop = pgraph->prop(ref->payload);
    auto [ptr, len] = pgraph->getPropData().get_blob<xsd::Type::String>(static_cast<int32_t>(prop.value));
    const char* raw = reinterpret_cast<const char*>(ptr);
    const auto langLen = static_cast<uint8_t>(raw[0]);
    return VarLen32::fromString(std::string(raw + 1 + langLen, len - 1 - langLen));
}

uint8_t* VariantRuntime::allocScratch(int64_t bytes) {
    return getCurrentExecutionContext()->allocString(static_cast<size_t>(bytes));
}

VarLen32 VariantRuntime::toStringNumeric(int64_t payload, int32_t tag) {
    auto lit = getLiteralFromNumeric(reinterpret_cast<uint8_t*>(&payload), tag);
    if (lit.null()) return emptyVarLen32();
    return VarLen32::fromString(static_cast<std::string>(lit));
}

VarLen32 VariantRuntime::toStringNodeRef(PropertyGraph::NodeEntry* ref) {
    PropertyGraph* pgraph = propertyGraphOf(ref);
    const int32_t localId = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(ref));
    return VarLen32::fromString(pgraph->getMetadata().get_node_name(localId));
}

VarLen32 VariantRuntime::toStringBlobLiteral(PropertyGraph::NodeEntry* ref) {
    auto lit = reconstructBlobLiteral(ref);
    if (!lit.has_value()) return emptyVarLen32();
    return VarLen32::fromString(static_cast<std::string>(*lit));
}

int8_t VariantRuntime::compareNodeRefRef(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate) {
    const int64_t lhsUid = resolveNodeRef(lhs);
    const int64_t rhsUid = resolveNodeRef(rhs);
    switch (predicate) {
        case 0: return lhsUid == rhsUid;
        case 1: return lhsUid != rhsUid;
        case 2: return lhsUid < rhsUid;
        case 3: return lhsUid <= rhsUid;
        case 4: return lhsUid > rhsUid;
        case 5: return lhsUid >= rhsUid;
        default: return -1;
    }
}

int8_t VariantRuntime::compareBlobLiteralRefRef(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate) {
    auto lhsLit = reconstructBlobLiteral(lhs);
    auto rhsLit = reconstructBlobLiteral(rhs);
    if (!lhsLit.has_value() || !rhsLit.has_value()) return -1;
    return triBoolToInt8(applyPredicate(*lhsLit, *rhsLit, predicate));
}

int8_t VariantRuntime::compareNumericCross(int64_t lhsPayload, int32_t lhsTag, int64_t rhsPayload, int32_t rhsTag, int32_t predicate) {
    auto lhsLit = getLiteralFromNumeric(reinterpret_cast<uint8_t*>(&lhsPayload), lhsTag);
    auto rhsLit = getLiteralFromNumeric(reinterpret_cast<uint8_t*>(&rhsPayload), rhsTag);
    if (lhsLit.null() || rhsLit.null()) return -1;
    return triBoolToInt8(applyPredicate(lhsLit, rhsLit, predicate));
}

int64_t VariantRuntime::hashNumeric(int64_t payload, int32_t tag) {
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&payload);
    auto t = xsd::from_int32(tag);
    if (!t.has_value()) return 0;
    if (*t == xsd::Type::Date) return payload;
    auto lit = getLiteralFromNumeric(ptr, tag);
    if (lit.null()) return 0;
    auto asDouble = lit.cast_to_value<rdf4cpp::datatypes::xsd::Double>();
    if (!asDouble.has_value()) return 0; // should not happen for the numeric-family tag set
    int64_t bits;
    std::memcpy(&bits, &*asDouble, sizeof(bits));
    return bits;
}

int32_t VariantRuntime::arithNumericCross(int64_t lhsPayload, int32_t lhsTag, int64_t rhsPayload, int32_t rhsTag, int32_t predicate, uint8_t* outPtr) {
    auto lhsLit = getLiteralFromNumeric(reinterpret_cast<uint8_t*>(&lhsPayload), lhsTag);
    auto rhsLit = getLiteralFromNumeric(reinterpret_cast<uint8_t*>(&rhsPayload), rhsTag);
    if (lhsLit.null() || rhsLit.null()) return xsd::to_int32(xsd::Type::Unspecified);
    auto result = applyArith(lhsLit, rhsLit, predicate);
    if (result.null()) return xsd::to_int32(xsd::Type::Unspecified);
    auto resultTag = static_cast<int32_t>(xsd::from_iri(result.datatype()));
    if (!extractNumericByTag(result, resultTag, outPtr)) return xsd::to_int32(xsd::Type::Unspecified);
    return resultTag;
}

int8_t VariantRuntime::compareOrder(int64_t lhsPayload, int32_t lhsTag, PropertyGraph::NodeEntry* lhsRef, int64_t rhsPayload, int32_t rhsTag, PropertyGraph::NodeEntry* rhsRef) {
    const bool lhsIsNode = lhsTag == xsd::to_int32(xsd::Type::RDFNode);
    const bool rhsIsNode = rhsTag == xsd::to_int32(xsd::Type::RDFNode);
    // Rank: blank node (0) < IRI (1) < literal (2).
    auto rankOf = [](bool isNode, PropertyGraph::NodeEntry* ref) -> std::pair<int, bool> {
        if (!isNode) return {2, false};
        PropertyGraph* pgraph = propertyGraphOf(ref);
        const int32_t localId = GraphStorage::nodeId(reinterpret_cast<uint8_t*>(ref));
        const bool isBlank = pgraph->getMetadata().type_id(localId) == static_cast<int32_t>(RDFNodeType::BNode);
        return {isBlank ? 0 : 1, isBlank};
    };
    const auto [lhsRank, lhsBlank] = rankOf(lhsIsNode, lhsRef);
    const auto [rhsRank, rhsBlank] = rankOf(rhsIsNode, rhsRef);
    if (lhsRank != rhsRank) return lhsRank < rhsRank ? -1 : 1;
    if (lhsRank != 2) {
        const std::string lhsName = propertyGraphOf(lhsRef)->getMetadata()
            .get_node_name(GraphStorage::nodeId(reinterpret_cast<uint8_t*>(lhsRef)));
        const std::string rhsName = propertyGraphOf(rhsRef)->getMetadata()
            .get_node_name(GraphStorage::nodeId(reinterpret_cast<uint8_t*>(rhsRef)));
        if (lhsName != rhsName) return lhsName < rhsName ? -1 : 1;
        return 0;
    }
    auto lhsLit = reconstructLiteral(lhsPayload, lhsTag, lhsRef);
    auto rhsLit = reconstructLiteral(rhsPayload, rhsTag, rhsRef);
    if (!lhsLit.has_value() || !rhsLit.has_value()) return 0;
    const auto ord = lhsLit->order(*rhsLit);
    if (ord == std::strong_ordering::less) return -1;
    if (ord == std::strong_ordering::greater) return 1;
    return 0;
}

int8_t VariantRuntime::langMatches(int64_t payload, int32_t tag, PropertyGraph::NodeEntry* ref, VarLen32 langRange) {
    auto lit = reconstructLiteral(payload, tag, ref);
    if (!lit.has_value()) return -1;
    return triBoolToInt8(lit->language_tag_matches_range(std::string_view(langRange.data(), langRange.getLen())));
}
