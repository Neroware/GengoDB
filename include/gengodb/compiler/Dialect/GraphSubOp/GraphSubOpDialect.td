#ifndef GENGODB_DIALECT_GRAPHSUBOP_DIALECT
#define GENGODB_DIALECT_GRAPHSUBOP_DIALECT

include "mlir/IR/OpBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/IR/SymbolInterfaces.td"
include "lingodb/compiler/Dialect/SubOperator/SubOperatorBase.td"

//===----------------------------------------------------------------------===//
// Graph dialect definition.
//===----------------------------------------------------------------------===//

def GraphSubOp_Dialect : Dialect {
    let name = "gsubop";
    let summary = "A dialect for universal graph processing";
    let cppNamespace = "::gengodb::compiler::dialect::gsubop";
    let useDefaultTypePrinterParser=1;
    let useDefaultAttributePrinterParser=1;
    let extraClassDeclaration = [{
            private:
            void registerTypes();
            void registerAttrs();
            NamedGraphManager namedGraphManager;
            public:
            NamedGraphManager& getNamedGraphManager() {
                return namedGraphManager;
            }
        }];
}

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

class GraphSubOp_Type<string name, string typeMnemonic, list<Trait> traits = []> : TypeDef<GraphSubOp_Dialect, name, traits> {
    let mnemonic = typeMnemonic;
}

def GraphSetIterator : GraphSubOp_Type<"GraphSetIterator", "graph_set_iterator"> {
    let summary = "graph set iterator";
    let parameters = (ins "mlir::ArrayAttr":$strategy);
    let assemblyFormat = "`<` $strategy `>`";
}

def NodeSet : GraphSubOp_Type<"NodeSet", "node_set", [State,LookupAbleState]> {
    let summary = "node set";
    let parameters = (ins "StateMembersAttr":$members);
    let assemblyFormat = "`<` custom<StateMembers>($members) `>`";
    let extraClassDeclaration = [{
            StateMembersAttr getKeyMembers(){
            return lingodb::compiler::dialect::subop::StateMembersAttr::get(getContext(), {});
            }
            StateMembersAttr getValueMembers(){
            return lingodb::compiler::dialect::subop::StateMembersAttr::get(getContext(), {});
            }
        }];
}
def EdgeSet : GraphSubOp_Type<"EdgeSet", "edge_set", [State,LookupAbleState]> {
    let summary = "edge set";
    let parameters = (ins "StateMembersAttr":$members);
    let assemblyFormat = "`<` custom<StateMembers>($members) `>`";
    let extraClassDeclaration = [{
            StateMembersAttr getKeyMembers(){
            return lingodb::compiler::dialect::subop::StateMembersAttr::get(getContext(), {});
            }
            StateMembersAttr getValueMembers(){
            return lingodb::compiler::dialect::subop::StateMembersAttr::get(getContext(), {});
            }
        }];
}

def Graph : GraphSubOp_Type<"Graph", "graph", [State]> {
    let summary = "universal graph type";
    let parameters = (ins "StateMembersAttr":$nodeMembers, "StateMembersAttr":$edgeMembers);
    let assemblyFormat = "`<` custom<StateMembers>($nodeMembers) `,` custom<StateMembers>($edgeMembers) `>`";
    let extraClassDeclaration = [{
        StateMembersAttr getMembers();
    }];
}

def NodeRef : GraphSubOp_Type<"NodeRef", "node_ref", [State]> {
    let summary = "graph node reference";
    let parameters = (ins "StateMembersAttr":$nodeMembers, "StateMembersAttr":$incomingMembers, "StateMembersAttr":$outgoingMembers, "StateMembersAttr":$propertyMembers);
    let assemblyFormat = "`<` custom<StateMembers>($nodeMembers) `,` custom<StateMembers>($incomingMembers) `,` custom<StateMembers>($outgoingMembers) `,` custom<StateMembers>($propertyMembers) `>`";
    let extraClassDeclaration = [{
        StateMembersAttr getMembers();
    }];
}
def EdgeRef : GraphSubOp_Type<"EdgeRef", "edge_ref", [State]> {
    let summary = "graph edge reference";
    let parameters = (ins "StateMembersAttr":$edgeMembers, "StateMembersAttr":$fromMembers, "StateMembersAttr":$toMembers, "StateMembersAttr":$propertyMembers);
    let assemblyFormat = "`<` custom<StateMembers>($edgeMembers) `,` custom<StateMembers>($fromMembers) `,` custom<StateMembers>($toMembers) `,` custom<StateMembers>($propertyMembers) `>`";
    let extraClassDeclaration = [{
        StateMembersAttr getMembers();
    }];
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Property Graph
////////////////////////////////////////////////////////////////////////////////////////////////////

// The following types are strictly runtime-specific for the given PropertyGraph data structure 
// adding the processing of Neo4j-style graphs to LingoDB. This means they are not necessary for a 
// general graph processing IR, which is minimally complete with types for graphs, node sets, nodes 
// edge sets and edges.

def PropertySet : GraphSubOp_Type<"PropertySet", "property_set", [State,LookupAbleState]> {
    let summary = "property set";
    let parameters = (ins "StateMembersAttr":$members);
    let assemblyFormat = "`<` custom<StateMembers>($members) `>`";
    let extraClassDeclaration = [{
            StateMembersAttr getKeyMembers(){
            return lingodb::compiler::dialect::subop::StateMembersAttr::get(getContext(), {});
            }
            StateMembersAttr getValueMembers(){
            return lingodb::compiler::dialect::subop::StateMembersAttr::get(getContext(), {});
            }
        }];
}
def PropertyRef : GraphSubOp_Type<"PropertyRef", "property_ref", []> {
    let summary = "property reference";
}
def TypedPropertyRef : GraphSubOp_Type<"TypedPropertyRef", "typed_property_ref", [State]> {
    let summary = "typed property ref";
    let parameters = (ins "StateMembersAttr":$members);
    let assemblyFormat = "`<` custom<StateMembers>($members) `>`";
}
def Identifier : GraphSubOp_Type<"Identifier", "identifier", []> {
    let summary = "identifier";
}

//===----------------------------------------------------------------------===//
// Attributes
//===----------------------------------------------------------------------===//

class GraphSubOp_Attr<string name> : AttrDef<GraphSubOp_Dialect, name>;

// Currently needed because otherwise attribute part of the codegen won't run causing a compiler error
def GraphSubOpDummyAttr : GraphSubOp_Attr<"GraphSubOpDummyAttr"> {
    let mnemonic = "gsubop_dummy_attr";
}

#endif // GENGODB_DIALECT_GRAPHSUBOP_DIALECT