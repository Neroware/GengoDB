#ifndef GENGODB_COMPILER_DIALECT_VARIANT_VARIANTOPSENUMS_H
#define GENGODB_COMPILER_DIALECT_VARIANT_VARIANTOPSENUMS_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/Variant/VariantOpsEnums.h.inc"

#endif // GENGODB_COMPILER_DIALECT_VARIANT_VARIANTOPSENUMS_H
