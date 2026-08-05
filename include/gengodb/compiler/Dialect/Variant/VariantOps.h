#ifndef GENGODB_COMPILER_DIALECT_VARIANT_VARIANTOPS_H
#define GENGODB_COMPILER_DIALECT_VARIANT_VARIANTOPS_H

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"
#include "gengodb/compiler/Dialect/Variant/VariantOpsEnums.h"

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/Variant/VariantOps.h.inc"

#endif // GENGODB_COMPILER_DIALECT_VARIANT_VARIANTOPS_H
