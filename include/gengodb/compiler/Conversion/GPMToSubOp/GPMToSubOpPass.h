#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_GPMTOSUBOPPASS_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_GPMTOSUBOPPASS_H
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include <memory>

namespace gengodb::compiler::dialect {
namespace gpm {
std::unique_ptr<mlir::Pass> createLowerToSubOpPass();
std::unique_ptr<mlir::Pass> createLowerGPMScalarsToSubOpPass();
void registerGPMToSubOpConversionPasses();
void createLowerGPMToSubOpPipeline(mlir::OpPassManager& pm);
// gpm.relalg.graph_expansion consumes a relational stream and is therefore lowered by RelAlgToSubOp
void populateGraphExpansionToSubOpPatterns(mlir::RewritePatternSet& patterns, mlir::TypeConverter& typeConverter);
} // end namespace relalg
} // end namespace lingodb::compiler::dialect
#endif //GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_GPMTOSUBOPPASS_H
