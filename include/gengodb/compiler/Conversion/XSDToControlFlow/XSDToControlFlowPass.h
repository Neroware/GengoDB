#ifndef GENGODB_COMPILER_CONVERSION_XSDTOCONTROLFLOW_XSDTOCONTROLFLOWPASS_H
#define GENGODB_COMPILER_CONVERSION_XSDTOCONTROLFLOW_XSDTOCONTROLFLOWPASS_H
#include "mlir/Pass/Pass.h"
#include <memory>

namespace gengodb::compiler::dialect {
namespace xsd {
std::unique_ptr<mlir::Pass> createLowerXSDToControlFlowPass();
void registerXSDToControlFlowConversionPasses();
} // end namespace xsd
} // end namespace gengodb::compiler::dialect
#endif //GENGODB_COMPILER_CONVERSION_XSDTOCONTROLFLOW_XSDTOCONTROLFLOWPASS_H
