#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include <llvm/ADT/TypeSwitch.h>

#define GET_TYPEDEF_CLASSES
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.cpp.inc"
void gengodb::compiler::dialect::gpm::GPMDialect::registerTypes() {
   addTypes<
#define GET_TYPEDEF_LIST
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.cpp.inc"

      >();
}
// #include "lingodb/compiler/Dialect/GPM/IR/GPMOpsTypeInterfaces.cpp.inc"