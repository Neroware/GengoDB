#ifndef GENGODB_DIALECT_GPM_IR_GPMINTERFACES
#define GENGODB_DIALECT_GPM_IR_GPMINTERFACES
include "mlir/IR/OpBase.td"

def GPMOperator : OpInterface<"GPMOperator"> {
let methods = [
InterfaceMethod<
    "get all variable bindings created by this operator",
    "lingodb::compiler::dialect::relalg::ColumnSet", "getCreatedVariables",(ins ),"", /*defaultImplementation=*/[{
    return gengodb::compiler::dialect::gpm::detail::getCreatedVariables(this->getOperation());
}]
>,
InterfaceMethod<
    "get all bound variables used by this operator",
    "lingodb::compiler::dialect::relalg::ColumnSet", "getBoundVariables",(ins ),"", /*defaultImplementation=*/[{
    return gengodb::compiler::dialect::gpm::detail::getBoundVariables(this->getOperation());
}]
>,
InterfaceMethod<
    "get all variables used by this operator",
    "lingodb::compiler::dialect::relalg::ColumnSet", "getUsedVariables",(ins ),"", /*defaultImplementation=*/[{
    return gengodb::compiler::dialect::gpm::detail::getUsedVariables(this->getOperation());
}]
>,
InterfaceMethod<
    "get variables that are available after this operator",
    "lingodb::compiler::dialect::relalg::ColumnSet", "getAvailableVariables",(ins ),"", /*defaultImplementation=*/[{
    return gengodb::compiler::dialect::gpm::detail::getAvailableVariables(this->getOperation());
}]
>,
];
}

def GraphPatternOp : OpInterface<"GraphPatternOp"> {
let methods = [
InterfaceMethod<
    "get the incoming tuple stream this pattern is applied to",
    "mlir::Value", "getRel",(ins )
>,
InterfaceMethod<
    "get the region containing the nested triple/pattern operators",
    "mlir::Region&", "getPattern",(ins )
>,
InterfaceMethod<
    "which graph pattern construct this operator represents (SPARQL BGP / OPTIONAL / MINUS)",
    "gengodb::compiler::dialect::gpm::PatternKind", "getPatternKind",(ins )
>,
InterfaceMethod<
    "collect the (subject,predicate,object) terms of all triple patterns nested (transitively) inside this pattern",
    "llvm::SmallVector<std::tuple<mlir::Attribute, mlir::Attribute, mlir::Attribute>, 16>", "getTriples",(ins ),"", /*defaultImplementation=*/[{
    return gengodb::compiler::dialect::gpm::detail::getPatternTriples(this->getOperation());
}]
>,
InterfaceMethod<
    "return a list containing the semantic data of all triple patterns directly nested in the pattern",
    "gengodb::compiler::dialect::gpm::TripleList", "getTripleList",(ins ),"", /*defaultImplementation=*/[{
    return gengodb::compiler::dialect::gpm::detail::getTripleList(this->getOperation());
}]
>,
];
}

#endif // GENGODB_DIALECT_GPM_IR_GPMINTERFACES