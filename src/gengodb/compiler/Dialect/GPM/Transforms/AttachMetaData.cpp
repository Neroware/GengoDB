#include "gengodb/catalog/GraphCatalogEntry.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.h"
#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
#include "lingodb/catalog/Catalog.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"

namespace {
using namespace gengodb::compiler::dialect;

class AttachMetaData : public mlir::PassWrapper<AttachMetaData, mlir::OperationPass<mlir::func::FuncOp>> {
   virtual llvm::StringRef getArgument() const override { return "gpm-attach-meta-data"; }
   lingodb::catalog::Catalog& catalog;

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AttachMetaData)
   AttachMetaData(lingodb::catalog::Catalog& catalog) : catalog(catalog) {}
   void runOnOperation() override {
      getOperation().walk([&](gpm::NamedGraphOp op) {
         auto graphRef = mlir::dyn_cast<gpm::GraphReferenceType>(op.getDef().getColumn().type);
         if (!graphRef) return;
         if (auto graph = catalog.getTypedEntry<gengodb::catalog::GraphCatalogEntry>(graphRef.getName().str())) {
            std::shared_ptr<gengodb::catalog::GraphMetadataProvider> meta = graph.value();
            op->setAttr("meta", gpm::GraphMetaDataAttr::get(&getContext(), meta));
         }
      });
   }
};
class DetachMetaData : public mlir::PassWrapper<DetachMetaData, mlir::OperationPass<mlir::func::FuncOp>> {
   virtual llvm::StringRef getArgument() const override { return "gpm-detach-meta-data"; }

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(DetachMetaData)
   void runOnOperation() override {
      getOperation().walk([&](gpm::NamedGraphOp op) {
         op->removeAttr("meta");
      });
   }
};
} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createAttachMetaDataPass(lingodb::catalog::Catalog& catalog) { return std::make_unique<AttachMetaData>(catalog); }
std::unique_ptr<mlir::Pass> gpm::createDetachMetaDataPass() { return std::make_unique<DetachMetaData>(); }
