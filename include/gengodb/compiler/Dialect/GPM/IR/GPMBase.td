#ifndef GENGODB_DIALECT_GPM_IR_OPS
#define GENGODB_DIALECT_GPM_IR_OPS

include "mlir/IR/OpBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/IR/SymbolInterfaces.td"
include "lingodb/compiler/Dialect/TupleStream/TupleStreamBase.td"

include "GPMInterfaces.td"

//===----------------------------------------------------------------------===//
// GPM dialect definition.
//===----------------------------------------------------------------------===//

def GPM_Dialect : Dialect {
    let name = "gpm";
    let summary = "A dialect for graph pattern matching";
    let cppNamespace = "::gengodb::compiler::dialect::gpm";
    let useDefaultTypePrinterParser=1;
    let useDefaultAttributePrinterParser=1;
    let extraClassDeclaration = [{
            private:
            void registerTypes();
            void registerAttrs();
        }];
}

//===----------------------------------------------------------------------===//
// Enums
//===----------------------------------------------------------------------===//

def GPM_TermType_IDENTIFIER  : I64EnumAttrCase<"identifier", 0>;
def GPM_TermType_BLANK  : I64EnumAttrCase<"blank", 1>;
def GPM_TermType_LITERAL  : I64EnumAttrCase<"literal", 2>;
def GPM_TermType_VARIABLE  : I64EnumAttrCase<"variable", 3>;

def GPM_TermType : I64EnumAttr<"TermType", "term type",
        [GPM_TermType_IDENTIFIER,GPM_TermType_BLANK,GPM_TermType_LITERAL,GPM_TermType_VARIABLE]> {
    let cppNamespace = "::gengodb::compiler::dialect::gpm";
}

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

class GPM_Type<string name, string typeMnemonic> : TypeDef<GPM_Dialect, name> {
    let mnemonic = typeMnemonic;
}

def VariableBinding : GPM_Type<"VariableBinding", "variable_binding"> {
    let summary = "variable binding type";
}

def GraphRef : GPM_Type<"GraphReference", "graph_ref"> {
    let summary = "graph reference";
}

//===----------------------------------------------------------------------===//
// Attributes
//===----------------------------------------------------------------------===//

class GPM_Attr<string name> : AttrDef<GPM_Dialect, name>;

class GPM_TermAttr<string name, string attrMnemonic> : GPM_Attr<name> {
    let mnemonic = attrMnemonic;
    let summary = "graph pattern term attribute";
    let extraClassDeclaration = [{
        TermType getTermType() const;
    }];
}

def IdentifierTermAttr : GPM_TermAttr<"IdentifierTerm", "identifier"> {
    let parameters = (ins "mlir::StringAttr":$ident);
    let summary = "identifier term";
    let extraClassDeclaration = [{
        TermType getTermType() const {  return TermType::identifier;  }
        std::string identifier() const {  return getIdent().getValue().str();  }
    }];
    let hasCustomAssemblyFormat = 1;
}

def BNodeTermAttr : GPM_TermAttr<"BNodeTerm", "blank_node"> {
    let parameters = (ins "mlir::StringAttr":$localId);
    let summary = "blank node term";
    let extraClassDeclaration = [{
        TermType getTermType() const {  return TermType::blank;  }
        std::string localId() const {  return getLocalId().getValue().str();  }
    }];
    let hasCustomAssemblyFormat = 1;
}

def VariableTermAttr : GPM_TermAttr<"VariableTerm", "variable"> {
    let parameters = (ins "mlir::Attribute":$binding);
    let summary = "variable term";
    let extraClassDeclaration = [{
        TermType getTermType() const {  return TermType::variable;  }
        bool hasBinding() const;
        lingodb::compiler::dialect::tuples::ColumnRefAttr getBindingReference() const;
        lingodb::compiler::dialect::tuples::ColumnDefAttr getProducedBinding() const;
    }];
    let hasCustomAssemblyFormat = 1;
}

#endif // #ifndef GENGODB_DIALECT_GPM_IR_OPS