#ifndef GENGODB_DIALECT_GPM_IR_GPMOPS
#define GENGODB_DIALECT_GPM_IR_GPMOPS

include "mlir/IR/OpBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/IR/SymbolInterfaces.td"
include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgInterfaces.td"
include "gengodb/compiler/Dialect/GPM/IR/GPMBase.td"

include "GPMInterfaces.td"

//===----------------------------------------------------------------------===//
// Base GPM operation definition.
//===----------------------------------------------------------------------===//

class GPM_Op<string mnemonic, list<Trait> traits = []> : Op<GPM_Dialect, mnemonic, traits> {
  let hasCustomAssemblyFormat = 1;
}

def TriplePatternOp : GPM_Op<"triple_pattern", [Pure,GPMOperator,Operator,UnaryOperator]> {
  let summary = "triple pattern";
  let arguments = (ins TupleStream:$rel, ColumnRefAttr:$graphRef, AnyAttr:$s, AnyAttr:$p, AnyAttr:$o);
  let results = (outs TupleStream:$res);
  let assemblyFormat = [{  $rel custom<CustRef>($graphRef) `(` custom<Term>($s) `,` custom<Term>($p) `,` custom<Term>($o) `)` attr-dict }];
  let hasVerifier = 1;
  let extraClassDeclaration = [{
    lingodb::compiler::dialect::relalg::ColumnSet getUsedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getCreatedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache);
    lingodb::compiler::dialect::relalg::ColumnSet getBindingColumns();
    bool canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column);
  }];
}

def NamedGraphOp : GPM_Op<"named_graph", [Pure,GPMOperator,Operator]> {
  let summary = "named graph";
  let arguments = (ins ColumnDefAttr:$def);
  let results = (outs TupleStream:$res);
  let assemblyFormat = [{  `column` `:` custom<CustDef>($def) attr-dict }];
  let extraClassDeclaration = [{
    lingodb::compiler::dialect::relalg::ColumnSet getUsedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getCreatedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache);
    bool canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column);
  }];
}

def BasicGraphPatternOp : GPM_Op<"basic_graph_pattern", [Pure,GPMOperator,GraphPatternOp,Operator,UnaryOperator]> {
  let summary = "basic graph pattern";
  let arguments = (ins TupleStream:$rel);
  let results = (outs TupleStream:$res);
  let regions = (region SizedRegion<1>:$pattern);
  let assemblyFormat = [{  $rel custom<CustRegion>($pattern) attr-dict  }];
  let extraClassDeclaration = [{
    gengodb::compiler::dialect::gpm::PatternKind getPatternKind() { return gengodb::compiler::dialect::gpm::PatternKind::basic; }
    lingodb::compiler::dialect::relalg::ColumnSet getBoundVariables() { return getUsedVariables(); }
    lingodb::compiler::dialect::relalg::ColumnSet getCreatedVariables();
    lingodb::compiler::dialect::relalg::ColumnSet getUsedVariables();
    lingodb::compiler::dialect::relalg::ColumnSet getUsedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getCreatedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache);
    bool canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column);
  }];
  let hasVerifier = 1;
}

def OptionalGraphPatternOp : GPM_Op<"optional_graph_pattern", [Pure,GPMOperator,GraphPatternOp,Operator,UnaryOperator]> {
  let summary = "optional graph pattern (SPARQL OPTIONAL)";
  let arguments = (ins TupleStream:$rel);
  let results = (outs TupleStream:$res);
  let regions = (region SizedRegion<1>:$pattern);
  let assemblyFormat = [{  $rel custom<CustRegion>($pattern) attr-dict  }];
  let extraClassDeclaration = [{
    gengodb::compiler::dialect::gpm::PatternKind getPatternKind() { return gengodb::compiler::dialect::gpm::PatternKind::optional; }
    lingodb::compiler::dialect::relalg::ColumnSet getBoundVariables() { return getUsedVariables(); }
    lingodb::compiler::dialect::relalg::ColumnSet getCreatedVariables();
    lingodb::compiler::dialect::relalg::ColumnSet getUsedVariables();
    lingodb::compiler::dialect::relalg::ColumnSet getUsedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getCreatedColumns();
    lingodb::compiler::dialect::relalg::ColumnSet getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache);
    bool canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column);
  }];
  let hasVerifier = 1;
}

def IdentifiersEqualOp : GPM_Op<"identifiers_equal", [Pure]> {
  let summary = "identifier equality check";
  let arguments = (ins AnyType:$lhs, AnyType:$rhs);
  let results = (outs I1:$result);
  let assemblyFormat = [{ $lhs `:` type($lhs) `,` $rhs `:` type($rhs) attr-dict }];
}

#endif // GENGODB_DIALECT_GPM_IR_GPMOPS