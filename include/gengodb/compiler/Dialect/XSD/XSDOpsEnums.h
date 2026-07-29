#ifndef GENGODB_COMPILER_DIALECT_XSD_XSDOPSENUMS_H
#define GENGODB_COMPILER_DIALECT_XSD_XSDOPSENUMS_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/XSD/XSDOpsEnums.h.inc"

#endif // GENGODB_COMPILER_DIALECT_XSD_XSDOPSENUMS_H
