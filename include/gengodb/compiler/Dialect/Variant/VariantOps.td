#ifndef GENGODB_DIALECT_VARIANT_OPS
#define GENGODB_DIALECT_VARIANT_OPS

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "gengodb/compiler/Dialect/Variant/VariantDialect.td"

// !variant.variant lowers to `tuple<i32, !util.ref<i8>>`: a tag (drawn from
// gengodb::semantics::xsd::Type, see Datatypes.h) plus a pointer to the first
// byte of the payload. The tag alone determines how many bytes to read and
// how to interpret them; there is no separate length field.

class Variant_Op<string mnemonic, list<Trait> traits = []> : Op<Variant_Dialect, mnemonic, traits>;

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

def CreateScalarOp : Variant_Op<"create_scalar", [Pure]> {
    let summary = "wrap a statically-typed scalar value in a variant";
    let description = [{
        `value`'s MLIR type must map to a fixed-width `xsd::Type` tag (see the
        VariantToStd lowering's type table). Lowering allocates a stack slot,
        stores `value` into it, and packs `{tag, ref-to-slot}`; no runtime
        call. The op is `Pure` even though its lowering emits a stack write:
        from the variant IR's perspective the resulting value is a pure
        function of its operand.
    }];
    let arguments = (ins AnyType:$value);
    let results = (outs Variant:$result);
    let assemblyFormat = [{ $value `:` type($value) attr-dict }];
}

def CreateNodeRefOp : Variant_Op<"create_node_ref", [Pure]> {
    let summary = "wrap a graph-bound RDF term reference in a variant";
    let description = [{
        `ref` is an opaque graph node pointer (lowers to `!util.ref<i8>`).
        Lowering makes one runtime call to resolve the node's tag (a cheap
        PropRecord field read; no RDF/rdf4cpp involved).
    }];
    let arguments = (ins AnyType:$ref);
    let results = (outs Variant:$result);
    let assemblyFormat = [{ $ref `:` type($ref) attr-dict }];
}

//===----------------------------------------------------------------------===//
// Inspection
//===----------------------------------------------------------------------===//

def VariantIsAOp : Variant_Op<"variant_is_a", [Pure]> {
    let summary = "check whether a variant's tag matches a given type";
    let description = [{
        Returns `true` iff the variant tag matches `typeId`.
    }];
    let arguments = (ins Variant:$var, I32:$typeId);
    let results = (outs I1:$result);
    let assemblyFormat = [{ $var `{` `type` `=` $typeId `}` attr-dict }];
}

def VariantGetValOp : Variant_Op<"variant_get_val", [Pure]> {
    let summary = "unsafe variant unwrap to a statically-typed value";
    let description = [{
        Strict tag match: undefined behavior if result type is
        incompatible with the variant value.
    }];
    let arguments = (ins Variant:$val);
    let results = (outs AnyType:$result);
    let assemblyFormat = [{ $val `->` type($result) attr-dict }];
}

def ToStringOp : Variant_Op<"to_string", [Pure]> {
    let summary = "render a variant's value as its canonical lexical string form";
    let description = [{
        Always lowers to a runtime call that dispatches on the variant's tag.
        This is the only variant op allowed to materialize a fresh string;
        every other op (`cmp`, `arith`, `variant_get_val`) works off tags and
        existing bytes without ever producing a lexical form.
    }];
    let arguments = (ins Variant:$val);
    let results = (outs AnyType:$result);
    let assemblyFormat = [{ $val `->` type($result) attr-dict }];
}

//===----------------------------------------------------------------------===//
// Comparison / arithmetic
//===----------------------------------------------------------------------===//

def CmpOp : Variant_Op<"cmp", [Pure]> {
    let summary = "XSD/SPARQL value-semantics comparison between two variants";
    let description = [{
        `result` is `!db.nullable<i1>`; null means "incomparable" (mismatched
        datatypes with no valid promotion). Same-tag fixed-width scalars
        compare as native loads and the string family via `util.varlen32_cmp`;
        RDFNode refs resolve both sides' global UID (via the graph node index)
        and compare those; only cross-tag numeric promotion and the rare
        non-fixed-width literal family need a full runtime value comparison.
    }];
    let arguments = (ins VariantCmpPredicateAttr:$predicate, Variant:$lhs, Variant:$rhs);
    let results = (outs AnyType:$result);
    let assemblyFormat = [{ $predicate $lhs `,` $rhs `->` type($result) attr-dict }];
}

def ArithOp : Variant_Op<"arith", [Pure]> {
    let summary = "binary XSD/SPARQL numeric arithmetic on two variants";
    let description = [{
        Returns a variant directly (not `db.nullable<variant>`): an invalid
        combination (e.g. incompatible datatypes, division by zero) produces
        a variant tagged `xsd::Type::Unspecified`, the sentinel already
        documented in Datatypes.h as "no value"; there is exactly one way
        to represent "no result" in this dialect.
    }];
    let arguments = (ins VariantArithPredicateAttr:$predicate, Variant:$lhs, Variant:$rhs);
    let results = (outs Variant:$result);
    let assemblyFormat = [{ $predicate $lhs `,` $rhs attr-dict }];
}

#endif // GENGODB_DIALECT_VARIANT_OPS
