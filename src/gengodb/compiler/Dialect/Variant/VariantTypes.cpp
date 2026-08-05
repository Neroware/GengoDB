#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include <llvm/ADT/TypeSwitch.h>

#define GET_TYPEDEF_CLASSES
#include "gengodb/compiler/Dialect/Variant/VariantOpsTypes.cpp.inc"

namespace gengodb::compiler::dialect::variant {
void VariantDialect::registerTypes() {
    addTypes<
    #define GET_TYPEDEF_LIST
    #include "gengodb/compiler/Dialect/Variant/VariantOpsTypes.cpp.inc"
    >();
}
} // namespace gengodb::compiler::dialect::variant
