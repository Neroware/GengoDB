#include "gengodb/compiler/Dialect/GraphSubOp/Transforms/Passes.h"
using namespace gengodb::compiler::dialect;

void gsubop::registerGraphSubOpTransformations() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gsubop::createStringifyMaterializedGraphRefsPass();
   });
}
