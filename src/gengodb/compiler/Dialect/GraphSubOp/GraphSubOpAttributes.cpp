#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsAttributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include <llvm/ADT/TypeSwitch.h>

#define GET_ATTRDEF_CLASSES
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsAttributes.cpp.inc"

void gengodb::compiler::dialect::gsubop::GraphSubOpDialect::registerAttrs() {
   addAttributes<
#define GET_ATTRDEF_LIST
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsAttributes.cpp.inc"

      >();
}