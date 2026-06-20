#ifndef GENGODB_DIALECT_GRAPHSUBOP_OPS
#define GENGODB_DIALECT_GRAPHSUBOP_OPS

include "mlir/IR/OpBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/IR/SymbolInterfaces.td"
include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.td"

//===----------------------------------------------------------------------===//
// Ops
//===----------------------------------------------------------------------===//

class GraphSubOp_Op<string mnemonic, list<Trait> traits = []> : Op<GraphSubOp_Dialect, mnemonic, traits> {
    let hasCustomAssemblyFormat = 1;
}

def CreateGraphOp : GraphSubOp_Op<"create_graph", [Pure,StateCreator]> {
    let summary = "create new graph";
    let arguments = (ins I64Attr : $numNodes, I64Attr : $numRels, I64Attr : $numProps);
    let results = (outs Graph : $res);
    let assemblyFormat = [{ `{` `nodes` `=` $numNodes `,` `relations` `=` $numRels `,` `properties` `=` $numProps `}` `:` type($res) attr-dict }];
}

def Graph_BuiltinGraph_BASIC : I64EnumAttrCase<"standard", 0>;
def Graph_BuiltinGraph_PAGERANK : I64EnumAttrCase<"pagerank", 1>;
def Graph_BuiltinGraph_PROPERTYGRAPH : I64EnumAttrCase<"propertygraph", 2>;
def Graph_BuiltinGraph : I64EnumAttr<"BuiltinGraphAttr", "",
        [Graph_BuiltinGraph_BASIC,Graph_BuiltinGraph_PAGERANK,Graph_BuiltinGraph_PROPERTYGRAPH]> {
    let cppNamespace = "::gengodb::compiler::dialect::gsubop";
}
def CreateBuiltinGraphOp : GraphSubOp_Op<"create_builtin_graph", [Pure,StateCreator]> {
    let summary = "create builtin graph";
    let arguments = (ins Graph_BuiltinGraph : $graphId);
    let results = (outs Graph : $res);
    let assemblyFormat = [{ `{` `builtin` `=` $graphId `}` `:` type($res) attr-dict }];
}

def GetExternalGraphOp : GraphSubOp_Op<"get_external_graph", [Pure,StateCreator]> {
    let summary = "get external graph";
    let arguments = (ins StrAttr : $name, StrAttr : $graphId);
    let results = (outs Graph : $res);
    let assemblyFormat = [{ `{` `name` `=`  $name `,` `id` `=` $graphId `}` `:` type($res) attr-dict }];
}

def ScanGraphOp : GraphSubOp_Op<"scan_graph", [Pure,SubOperator,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "scan state (sequentially)";
    let arguments = (ins AnyType : $graph, ColumnDefAttr: $nodeSet, ColumnDefAttr: $edgeSet);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{ $graph `:` type($graph) custom<CustDef>($nodeSet) `,` custom<CustDef>($edgeSet) attr-dict }];
    let extraClassDeclaration = [{
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

def ScanNodeSetOp : GraphSubOp_Op<"scan_node_set", [Pure,SubOperator,ReferenceProducer,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "scan state (sequentially)";
    let arguments = (ins AnyType : $node_set, ColumnDefAttr: $ref);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{ $node_set `:` type($node_set) custom<CustDef>($ref) attr-dict }];
    let extraClassDeclaration = [{
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedReference(){ return getRef(); }
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}
def ScanEdgeSetOp : GraphSubOp_Op<"scan_edge_set", [Pure,SubOperator,ReferenceProducer,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "scan state (sequentially)";
    let arguments = (ins AnyType : $edge_set, ColumnDefAttr: $ref);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{ $edge_set `:` type($edge_set) custom<CustDef>($ref) attr-dict }];
    let extraClassDeclaration = [{
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedReference(){ return getRef(); }
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

def NodeCountOp : GraphSubOp_Op<"node_count", [Pure,SubOperator,ReferenceProducer,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "graph node count";

    let arguments = (ins TupleStream : $stream, Graph : $graph, ColumnDefAttr:$ref);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{  $stream `,` $graph `->` custom<CustDef>($ref) `:` type($graph) attr-dict }];
    let extraClassDeclaration = [{
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedReference(){ return getRef(); }
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

def EdgeCountOp : GraphSubOp_Op<"edge_count", [Pure,SubOperator,ReferenceProducer,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "graph edge count";

    let arguments = (ins TupleStream : $stream, Graph : $graph, ColumnDefAttr:$ref);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{  $stream `,` $graph `->` custom<CustDef>($ref) `:` type($graph) attr-dict }];
    let extraClassDeclaration = [{
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedReference(){ return getRef(); }
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

def NodeDegreeOp : GraphSubOp_Op<"node_degree", [Pure,SubOperator,ReferenceProducer,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "node degree op";

    let arguments = (ins TupleStream : $stream, Graph : $graph, ColumnDefAttr:$ref, StrAttr:$dir);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{  $stream `,` $graph `[` $dir `]` `->` custom<CustDef>($ref) `:` type($graph) attr-dict }];
    let extraClassDeclaration = [{
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedReference(){ return getRef(); }
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Property Graph
////////////////////////////////////////////////////////////////////////////////////////////////////

// The following operations are strictly runtime-specific for the given PropertyGraph data structure 
// adding the processing of Neo4j-style graphs to LingoDB. This means they are not necessary for a 
// general graph processing IR, which is minimally complete with lowerings for CreateOp, ScanOp, 
// GatherOp and ScatterOp on node and edge sets.

def ScanPropertySetOp : GraphSubOp_Op<"scan_property_set", [Pure,SubOperator,ReferenceProducer,DeclareOpInterfaceMethods<StateUsingSubOperator>]> {
    let summary = "scan state (sequentially)";
    let arguments = (ins AnyType : $prop_set, ColumnDefAttr: $ref);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{ $prop_set `:` type($prop_set) custom<CustDef>($ref) attr-dict }];
    let extraClassDeclaration = [{
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedReference(){ return getRef(); }
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

//===----------------------------------------------------------------------===//
// Relationship filtering by identifier
//===----------------------------------------------------------------------===//

def CreateIdentifierOp : GraphSubOp_Op<"create_identifier", [Pure]> {
    let summary = "create type identifier";
    let arguments = (ins StrAttr:$ident);
    let results = (outs Identifier : $res);
    let assemblyFormat = [{  $ident `:` type($res) attr-dict }];
}

def CreateIdentifierStateOp : GraphSubOp_Op<"create_identifier_state", [Pure,StateCreator]> {
    let summary = "create type identifier state";
    let arguments = (ins StrAttr:$ident);
    let results = (outs SimpleState : $res);
    let assemblyFormat = [{  $ident `:` type($res) attr-dict }];
}

def FilterByIdentifierOp : GraphSubOp_Op<"filter_by_identifier", [Pure,SubOperator]> {
    let summary = "filter graph ref by identifier";
    let arguments = (ins TupleStream : $stream, ColumnRefAttr : $ref, Identifier : $ident);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{  $stream custom<CustRef>($ref) `[` $ident `]` attr-dict }];
    let extraClassDeclaration = [{
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

def CastPropertyRefOp : GraphSubOp_Op<"cast_property_ref", [Pure,SubOperator]> {
    let summary = "cast to typed property ref";
    let arguments = (ins TupleStream : $stream, ColumnRefAttr : $ref, ColumnDefAttr : $typedRef);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{  $stream custom<CustRef>($ref) `->` custom<CustDef>($typedRef) attr-dict }];
    let extraClassDeclaration = [{
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

def GraphRefToStringOp : GraphSubOp_Op<"ref_to_str", [Pure,SubOperator]> {
    let summary = "create string column from graph ref";
    let arguments = (ins TupleStream : $stream, ColumnRefAttr : $ref, ColumnDefAttr : $strRef);
    let results = (outs TupleStream : $res);
    let assemblyFormat = [{  $stream custom<CustRef>($ref) `->` custom<CustDef>($strRef) attr-dict }];
    let extraClassDeclaration = [{
        mlir::Operation* cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, lingodb::compiler::dialect::subop::ColumnMapping& columnMapping);
    }];
}

#endif // GENGODB_DIALECT_GRAPHSUBOP_OPS