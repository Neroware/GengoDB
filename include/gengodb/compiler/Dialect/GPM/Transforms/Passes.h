#ifndef GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_PASSES_H
#define GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_PASSES_H
#include "mlir/Pass/Pass.h"
#include <memory>
namespace gengodb::compiler::dialect {
namespace gpm {
std::unique_ptr<mlir::Pass> createUnnestGraphPatternsPass();
std::unique_ptr<mlir::Pass> createCreateRelAlgInFlightsPass();
std::unique_ptr<mlir::Pass> createPrepareRelAlgLoweringPass();
void registerGpmTransformations();
} // end namespace subop
} // end namespace lingodb::compiler::dialect

#endif //GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_PASSES_H
