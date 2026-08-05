#ifndef GENGODB_COMPILER_DIALECT_GPM_IR_GPMOPSATTRIBUTES_H
#define GENGODB_COMPILER_DIALECT_GPM_IR_GPMOPSATTRIBUTES_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOpsAttributes.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsEnums.h"

#define GET_ATTRDEF_CLASSES
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.h.inc"

#endif // GENGODB_COMPILER_DIALECT_GPM_IR_GPMOPSATTRIBUTES_H