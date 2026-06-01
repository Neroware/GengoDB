#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include <llvm/ADT/TypeSwitch.h>

#define GET_ATTRDEF_CLASSES
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.cpp.inc"

namespace gengodb::compiler::dialect::gpm {
using namespace lingodb::compiler::dialect;

bool VariableTermAttr::hasBinding() const {
    return mlir::isa<tuples::ColumnRefAttr>(getBinding());
}
tuples::ColumnRefAttr VariableTermAttr::getBindingReference() const {
    return mlir::dyn_cast_or_null<tuples::ColumnRefAttr>(getBinding());
}
tuples::ColumnDefAttr VariableTermAttr::getProducedBinding() const {
    return mlir::dyn_cast_or_null<tuples::ColumnDefAttr>(getBinding());
}
void IdentifierTermAttr::print(mlir::AsmPrinter &printer) const {
    printer << "id{" << getIdent() << "}";
}
::mlir::Attribute IdentifierTermAttr::parse(::mlir::AsmParser &parser, ::mlir::Type odsType) {
    auto context = parser.getContext();
    std::string ident;
    if (parser.parseKeyword("id") || parser.parseLBrace() 
        || parser.parseString(&ident) || parser.parseRBrace()) {
            return {};
    }
    return IdentifierTermAttr::get(context, mlir::StringAttr::get(context, ident));
}
void BNodeTermAttr::print(mlir::AsmPrinter &printer) const {
    printer << "_{" << getLocalId() << "}";
}
::mlir::Attribute BNodeTermAttr::parse(::mlir::AsmParser &parser, ::mlir::Type odsType) {
    auto context = parser.getContext();
    std::string localId;
    if (parser.parseKeyword("_") || parser.parseLBrace() 
        || parser.parseString(&localId) || parser.parseRBrace()) {
            return {};
    }
    return BNodeTermAttr::get(context, mlir::StringAttr::get(context, localId));
}
void VariableTermAttr::print(mlir::AsmPrinter &printer) const {
    printer << "?{" << getBinding() << "}";
}
::mlir::Attribute VariableTermAttr::parse(::mlir::AsmParser &parser, ::mlir::Type odsType) {
    auto context = parser.getContext();
    if (parser.parseQuestion() || parser.parseLBrace()) {
        return {};
    }
    mlir::Attribute binding;
    binding = tuples::ColumnDefAttr::parse(parser, odsType);
    if (!binding) {
        binding = tuples::ColumnRefAttr::parse(parser, odsType);
    }
    if (!binding || parser.parseRBrace()) {
        return {};
    }
    return VariableTermAttr::get(context, binding);
}

} // namespace gengodb::compiler::dialect::gpm

void gengodb::compiler::dialect::gpm::GPMDialect::registerAttrs() {
   addAttributes<
#define GET_ATTRDEF_LIST
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.cpp.inc"

      >();
}