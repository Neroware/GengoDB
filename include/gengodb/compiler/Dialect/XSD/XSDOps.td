#ifndef GENGODB_DIALECT_XSD_OPS
#define GENGODB_DIALECT_XSD_OPS

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "gengodb/compiler/Dialect/XSD/XSDDialect.td"

// A dynamic XSD value flowing through an arithmetic (sub-)expression is represented
// as a pair of SSA values traveling together: a `!db.nullable<!db.string>` lexical
// form and a `!db.nullable<i32>` XSD type tag (see gengodb::semantics::xsd::Type).
// Null on either half means "error" (unbound reference, non-numeric operand,
// division by zero, ...), SPARQL's third truth value, same null-bit convention
// `xsd.compare` already uses for its `!db.nullable<i1>` result, so it composes
// with the same `db.and`/`db.or`/`db.not` machinery once reduced back to a boolean by
// `xsd.compare_dyn`.

class XSD_Op<string mnemonic, list<Trait> traits = []> : Op<XSD_Dialect, mnemonic, traits>;

//===----------------------------------------------------------------------===//
// Comparison
//===----------------------------------------------------------------------===//

def CompareOp : XSD_Op<"compare", [Pure]> {
    let summary = "XSD/SPARQL value-semantics comparison between two RDF terms bound to variables";
    let description = [{
        Compares `lhs`/`rhs` per XSD/SPARQL value equality and ordering (numeric operands
        are compared after XSD numeric type promotion; strings/dates lexically/temporally).
        Both operands are raw graph refs (whose XSD datatype is only known at runtime) or
        `gsubop.wrap_nullable_ref`-wrapped graph refs (an unbound/null reference is treated
        as a comparison error, per SPARQL's "referencing an unbound variable is a type
        error" rule).

        Produces `!db.nullable<i1>`: the null bit means "comparison error" (incomparable
        datatypes or an unbound operand) SPARQL's third truth value (not SQL NULL, though
        represented the same way so it composes directly with `db.and`/`db.or`/`db.not`/
        `db.derive_truth`).
    }];
    let arguments = (ins XsdCmpPredicateAttr:$predicate, AnyType:$lhs, AnyType:$rhs);
    let results = (outs AnyType:$result);
    let assemblyFormat = [{ $predicate $lhs `:` type($lhs) `,` $rhs `:` type($rhs) `->` type($result) attr-dict }];
}

def CompareLiteralOp : XSD_Op<"compare_literal", [Pure]> {
    let summary = "XSD/SPARQL value-semantics comparison of a graph-bound RDF term against a query-text literal";
    let description = [{
        Like `xsd.compare`, but `rhs` is a literal written directly in the query
        text. Its XSD type and lexical form are compile-time-known constants, given as
        attributes rather than a second graph ref (a query-text literal was never inserted
        into the graph's dictionary, so there is no ref to compare against). `lhs` is the
        graph-bound side, same operand shape (possibly nullable-wrapped) as
        `xsd.compare`'s `lhs`.
    }];
    let arguments = (ins XsdCmpPredicateAttr:$predicate, AnyType:$lhs, StrAttr:$rhsLexicalForm, I32Attr:$rhsXsdType);
    let results = (outs AnyType:$result);
    let assemblyFormat = [{ $predicate $lhs `:` type($lhs) `,` $rhsLexicalForm `dtype` $rhsXsdType `->` type($result) attr-dict }];
}

//===----------------------------------------------------------------------===//
// FILTER arithmetic (+ - * /, unary -)
//===----------------------------------------------------------------------===//

def LiteralOfRefOp : XSD_Op<"literal_of_ref", [Pure]> {
    let summary = "materialize a graph-bound RDF term into the dynamic XSD value pair";
    let arguments = (ins AnyType:$ref);
    let results = (outs AnyType:$lexicalForm, AnyType:$xsdType);
    let assemblyFormat = [{ $ref `:` type($ref) `->` type($lexicalForm) `,` type($xsdType) attr-dict }];
}

def ArithOp : XSD_Op<"arith", [Pure]> {
    let summary = "binary XSD/SPARQL numeric arithmetic (+ - * /) on dynamic XSD values";
    let arguments = (ins XsdArithPredicateAttr:$predicate,
        AnyType:$lhsLexical, AnyType:$lhsType, AnyType:$rhsLexical, AnyType:$rhsType);
    let results = (outs AnyType:$lexicalForm, AnyType:$xsdType);
    let assemblyFormat = [{ $predicate $lhsLexical `:` type($lhsLexical) `,` $lhsType `:` type($lhsType) `,` $rhsLexical `:` type($rhsLexical) `,` $rhsType `:` type($rhsType) `->` type($lexicalForm) `,` type($xsdType) attr-dict }];
}

def NegateOp : XSD_Op<"negate", [Pure]> {
    let summary = "unary XSD/SPARQL numeric negation (-) on a dynamic XSD value";
    let arguments = (ins AnyType:$lexical, AnyType:$xsdType);
    let results = (outs AnyType:$lexicalForm, AnyType:$xsdTypeResult);
    let assemblyFormat = [{ $lexical `:` type($lexical) `,` $xsdType `:` type($xsdType) `->` type($lexicalForm) `,` type($xsdTypeResult) attr-dict }];
}

def CompareDynOp : XSD_Op<"compare_dyn", [Pure]> {
    let summary = "XSD/SPARQL value-semantics comparison of two dynamic XSD values";
    let arguments = (ins XsdCmpPredicateAttr:$predicate,
        AnyType:$lhsLexical, AnyType:$lhsType, AnyType:$rhsLexical, AnyType:$rhsType);
    let results = (outs AnyType:$result);
    let assemblyFormat = [{ $predicate $lhsLexical `:` type($lhsLexical) `,` $lhsType `:` type($lhsType) `,` $rhsLexical `:` type($rhsLexical) `,` $rhsType `:` type($rhsType) `->` type($result) attr-dict }];
}

#endif // GENGODB_DIALECT_XSD_OPS
