#ifndef GENGODB_DIALECT_VARIANT_DIALECT
#define GENGODB_DIALECT_VARIANT_DIALECT

include "mlir/IR/OpBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/IR/AttrTypeBase.td"

//===----------------------------------------------------------------------===//
// Variant dialect definition.
//===----------------------------------------------------------------------===//

def Variant_Dialect : Dialect {
    let name = "variant";
    let summary = "A dynamically-typed wrapper of a type union (see Datatypes.h)";
    let description = [{
        A dialect to support data models without a schema: In data models such as
        RDF a SPARQL variable can be bound to several types (an IRI, a blank node,
        or a literal of any XSD datatype, and which one it is is only known at 
        runtime. `!variant.variant` is a single runtime-tagged value (see Datatypes.h).
        It is intentionally standalone and does not depend on any dialects, so future
        data models without a schema can use it as well.
    }];
    let cppNamespace = "::gengodb::compiler::dialect::variant";
    let useDefaultTypePrinterParser = 1;
    let extraClassDeclaration = [{
        private:
        void registerTypes();
    }];
}

class Variant_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<Variant_Dialect, name, traits> {
    let mnemonic = typeMnemonic;
}

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

def Variant : Variant_Type<"Variant", "variant"> {
    let summary = "a runtime-tagged RDF term (IRI, blank node, or XSD-typed literal)";
}

//===----------------------------------------------------------------------===//
// Enums
//===----------------------------------------------------------------------===//

def VariantCmp_EQ  : I64EnumAttrCase<"eq", 0>;
def VariantCmp_NEQ : I64EnumAttrCase<"neq", 1>;
def VariantCmp_LT  : I64EnumAttrCase<"lt", 2>;
def VariantCmp_LTE : I64EnumAttrCase<"lte", 3>;
def VariantCmp_GT  : I64EnumAttrCase<"gt", 4>;
def VariantCmp_GTE : I64EnumAttrCase<"gte", 5>;
def VariantCmpPredicateAttr : I64EnumAttr<"VariantCmpPredicate", "",
        [VariantCmp_EQ, VariantCmp_NEQ, VariantCmp_LT, VariantCmp_LTE, VariantCmp_GT, VariantCmp_GTE]> {
    let cppNamespace = "::gengodb::compiler::dialect::variant";
}

def VariantArith_ADD : I64EnumAttrCase<"add", 0>;
def VariantArith_SUB : I64EnumAttrCase<"sub", 1>;
def VariantArith_MUL : I64EnumAttrCase<"mul", 2>;
def VariantArith_DIV : I64EnumAttrCase<"div", 3>;
def VariantArithPredicateAttr : I64EnumAttr<"VariantArithPredicate", "",
        [VariantArith_ADD, VariantArith_SUB, VariantArith_MUL, VariantArith_DIV]> {
    let cppNamespace = "::gengodb::compiler::dialect::variant";
}

#endif // GENGODB_DIALECT_VARIANT_DIALECT
