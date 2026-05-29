#ifndef GENGODB_COMPILER_DIALECT_GRAPHSUBOP_GRAPHSUBOPS_H
#define GENGODB_COMPILER_DIALECT_GRAPHSUBOP_GRAPHSUBOPS_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/LoopLikeInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "lingodb/compiler/Dialect/TupleStream/Column.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOpsAttributes.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOpsTypes.h"

#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsAttributes.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorInterfaces.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsTypes.h"

#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsEnums.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsAttributes.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h.inc"

#endif // GENGODB_COMPILER_DIALECT_GRAPHSUBOP_GRAPHSUBOPS_H
