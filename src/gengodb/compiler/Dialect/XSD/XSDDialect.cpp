#include "gengodb/compiler/Dialect/XSD/XSDDialect.h"

#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/XSD/XSDOps.h"

using namespace gengodb::compiler::dialect::xsd;

void XSDDialect::initialize() {
    addOperations<
    #define GET_OP_LIST
    #include "gengodb/compiler/Dialect/XSD/XSDOps.cpp.inc"
    >();
}
#include "gengodb/compiler/Dialect/XSD/XSDDialect.cpp.inc"
