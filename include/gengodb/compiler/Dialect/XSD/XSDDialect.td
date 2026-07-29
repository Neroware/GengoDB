#ifndef GENGODB_DIALECT_XSD_DIALECT
#define GENGODB_DIALECT_XSD_DIALECT

include "mlir/IR/OpBase.td"
include "mlir/IR/EnumAttr.td"

//===----------------------------------------------------------------------===//
// XSD dialect definition.
//===----------------------------------------------------------------------===//

def XSD_Dialect : Dialect {
    let name = "xsd";
    let summary = "XSD/SPARQL value-semantics comparison and arithmetic over RDF terms";
    let cppNamespace = "::gengodb::compiler::dialect::xsd";
    let dependentDialects = ["::gengodb::compiler::dialect::gsubop::GraphSubOpDialect"];
}

//===----------------------------------------------------------------------===//
// Enums
//===----------------------------------------------------------------------===//

def XsdCmp_EQ  : I64EnumAttrCase<"eq", 0>;
def XsdCmp_NEQ : I64EnumAttrCase<"neq", 1>;
def XsdCmp_LT  : I64EnumAttrCase<"lt", 2>;
def XsdCmp_LTE : I64EnumAttrCase<"lte", 3>;
def XsdCmp_GT  : I64EnumAttrCase<"gt", 4>;
def XsdCmp_GTE : I64EnumAttrCase<"gte", 5>;
def XsdCmpPredicateAttr : I64EnumAttr<"XsdCmpPredicate", "",
        [XsdCmp_EQ, XsdCmp_NEQ, XsdCmp_LT, XsdCmp_LTE, XsdCmp_GT, XsdCmp_GTE]> {
    let cppNamespace = "::gengodb::compiler::dialect::xsd";
}

def XsdArith_ADD : I64EnumAttrCase<"add", 0>;
def XsdArith_SUB : I64EnumAttrCase<"sub", 1>;
def XsdArith_MUL : I64EnumAttrCase<"mul", 2>;
def XsdArith_DIV : I64EnumAttrCase<"div", 3>;
def XsdArithPredicateAttr : I64EnumAttr<"XsdArithPredicate", "",
        [XsdArith_ADD, XsdArith_SUB, XsdArith_MUL, XsdArith_DIV]> {
    let cppNamespace = "::gengodb::compiler::dialect::xsd";
}

#endif // GENGODB_DIALECT_XSD_DIALECT
