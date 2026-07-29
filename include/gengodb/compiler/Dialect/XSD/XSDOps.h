#ifndef GENGODB_COMPILER_DIALECT_XSD_XSDOPS_H
#define GENGODB_COMPILER_DIALECT_XSD_XSDOPS_H

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "gengodb/compiler/Dialect/XSD/XSDOpsEnums.h"

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/XSD/XSDOps.h.inc"

#endif // GENGODB_COMPILER_DIALECT_XSD_XSDOPS_H
