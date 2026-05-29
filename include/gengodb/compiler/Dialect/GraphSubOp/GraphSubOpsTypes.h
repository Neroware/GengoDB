#ifndef GENGODB_COMPILER_DIALECT_GRAPHSUBOP_GRAPHSUBOPSTYPES_H
#define GENGODB_COMPILER_DIALECT_GRAPHSUBOP_GRAPHSUBOPSTYPES_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"

using namespace lingodb::compiler::dialect::subop;

#define GET_TYPEDEF_CLASSES
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h.inc"

#endif // GENGODB_COMPILER_DIALECT_GRAPHSUBOP_GRAPHSUBOPSTYPES_H
