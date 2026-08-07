#ifndef GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTTOSTDPASS_H
#define GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTTOSTDPASS_H
#include "mlir/Pass/Pass.h"
#include <memory>

namespace gengodb::compiler::dialect {
namespace variant {
std::unique_ptr<mlir::Pass> createLowerVariantToStdPass();
void registerVariantToStdConversionPasses();
} // end namespace variant
} // end namespace gengodb::compiler::dialect
#endif //GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTTOSTDPASS_H
