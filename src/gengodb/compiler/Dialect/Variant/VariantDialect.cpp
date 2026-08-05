#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"

#include "gengodb/compiler/Dialect/Variant/VariantOps.h"

using namespace gengodb::compiler::dialect::variant;

void VariantDialect::initialize() {
    addOperations<
    #define GET_OP_LIST
    #include "gengodb/compiler/Dialect/Variant/VariantOps.cpp.inc"
    >();
    registerTypes();
}
#include "gengodb/compiler/Dialect/Variant/VariantDialect.cpp.inc"
