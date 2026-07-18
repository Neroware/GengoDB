#ifndef GENGODB_COMPILER_DIALECT_GRAPHSUBOP_TRANSFORMS_PASSES_H
#define GENGODB_COMPILER_DIALECT_GRAPHSUBOP_TRANSFORMS_PASSES_H
#include "mlir/Pass/Pass.h"
#include <memory>
namespace gengodb::compiler::dialect {
namespace gsubop {
std::unique_ptr<mlir::Pass> createStringifyMaterializedGraphRefsPass();
void registerGraphSubOpTransformations();
} // end namespace gsubop
} // end namespace gengodb::compiler::dialect

#endif //GENGODB_COMPILER_DIALECT_GRAPHSUBOP_TRANSFORMS_PASSES_H
