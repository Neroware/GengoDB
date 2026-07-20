#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
using namespace gengodb::compiler::dialect;

void gpm::registerGpmTransformations() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gpm::createUnnestGraphPatternsPass();
   });
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gpm::createCreateRelAlgInFlightsPass();
   });
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gpm::createPrepareRelAlgLoweringPass();
   });
}