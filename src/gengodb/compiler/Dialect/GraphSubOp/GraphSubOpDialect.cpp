#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"

#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/DialectImplementation.h"

#include "llvm/ADT/TypeSwitch.h"

using namespace gengodb::compiler::dialect::gsubop;

void GraphSubOpDialect::initialize() {
    addOperations<
    #define GET_OP_LIST
    #include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.cpp.inc"
    
          >();
    
    registerTypes();
    registerAttrs();
}
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsDialect.cpp.inc"