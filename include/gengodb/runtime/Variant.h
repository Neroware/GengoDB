#ifndef GENGODB_RUNTIME_VARIANTRUNTIME_H
#define GENGODB_RUNTIME_VARIANTRUNTIME_H

#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {
using VarLen32 = lingodb::runtime::VarLen32;

struct VariantRuntime {
    // Tag of a literal or RDFNode ref.
    static int32_t resolveRefTag(PropertyGraph::NodeEntry* ref);

    // Global semantic-index UID for an RDFNode ref, via PropertyGraph::Metadata::uid().
    static int64_t resolveNodeRef(PropertyGraph::NodeEntry* ref);

    // Fixed-width numeric/date literal -> raw bytes, read directly from
    // PropRecord.value or PropertyData's i64/ui64/double vectors.
    static void extractNumericLiteral(PropertyGraph::NodeEntry* ref, int32_t tag, uint8_t* outPtr);

    // Blob-backed literal (string and everything without a fixed/inline shape)
    static VarLen32 extractBlobLiteral(PropertyGraph::NodeEntry* ref);

    // Per-query/per-worker arena allocation
    static uint8_t* allocScratch(int64_t bytes);

    static VarLen32 toStringNumeric(int64_t payload, int32_t tag);
    static VarLen32 toStringNodeRef(PropertyGraph::NodeEntry* ref);
    static VarLen32 toStringBlobLiteral(PropertyGraph::NodeEntry* ref);

    // Node-ref comparison: resolves both sides' global UID (possibly from
    // different graphs) and compares those.
    static int8_t compareNodeRefRef(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate);

    // Rare-tag literal comparison fallback (needs full XSD value semantics).
    static int8_t compareBlobLiteralRefRef(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate);

    // Cross-tag numeric comparison/arithmetic: tag + raw bytes -> rdf4cpp::Literal
    // via compile-time datatype tags, no IRI/graph lookup involved; genuinely
    // needs rdf4cpp for XSD numeric promotion rules.
    static int8_t compareNumericCross(int64_t lhsPayload, int32_t lhsTag, int64_t rhsPayload, int32_t rhsTag, int32_t predicate);
    static int32_t arithNumericCross(int64_t lhsPayload, int32_t lhsTag, int64_t rhsPayload, int32_t rhsTag, int32_t predicate, uint8_t* outPtr);

    // Canonicalizing hash for the fixed/inline numeric family
    static int64_t hashNumeric(int64_t payload, int32_t tag);

    // Total order of the variant type
    static int8_t compareOrder(int64_t lhsPayload, int32_t lhsTag, PropertyGraph::NodeEntry* lhsRef, int64_t rhsPayload, int32_t rhsTag, PropertyGraph::NodeEntry* rhsRef);

    // xsd:string language tag filter
    static int8_t langMatches(int64_t payload, int32_t tag, PropertyGraph::NodeEntry* ref, VarLen32 langRange);
};

} // namespace lingodb::runtime

#endif // GENGODB_RUNTIME_VARIANTRUNTIME_H
