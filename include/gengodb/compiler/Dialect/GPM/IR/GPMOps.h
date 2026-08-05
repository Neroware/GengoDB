#ifndef GENGODB_COMPILER_DIALECT_GPM_IR_GPMOPS_H
#define GENGODB_COMPILER_DIALECT_GPM_IR_GPMOPS_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/LoopLikeInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "lingodb/compiler/Dialect/TupleStream/Column.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOpsAttributes.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOpsTypes.h"

#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOpsInterfaces.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsEnums.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsInterfaces.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h.inc"

#endif // GENGODB_COMPILER_DIALECT_GPM_IR_GPMOPS_H
