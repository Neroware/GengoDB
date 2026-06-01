#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/DialectImplementation.h"

#include "llvm/ADT/TypeSwitch.h"

using namespace gengodb::compiler::dialect::gpm;

void GPMDialect::initialize() {
    addOperations<
    #define GET_OP_LIST
    #include "gengodb/compiler/Dialect/GPM/IR/GPMOps.cpp.inc"
    
          >();
    
    registerTypes();
    registerAttrs();
}
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsDialect.cpp.inc"