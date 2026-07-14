#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsTypes.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "lingodb/compiler/helper.h"

namespace {
using namespace gengodb::compiler::dialect;
using namespace lingodb::compiler::dialect;

class CreateInFlightOps : public mlir::RewritePattern {
    public:
    CreateInFlightOps(mlir::MLIRContext* context)
        : RewritePattern(relalg::MaterializeOp::getOperationName(), 1, context) {}
    mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
        if (mlir::isa<gpm::InFlightOp>(op) || mlir::isa<GPMOperator>(op) 
            || !mlir::isa<Operator>(op)) return mlir::failure();
        if (auto unaryOp = mlir::dyn_cast_or_null<UnaryOperator>(op)) {
            // TODO implement
        }
        else if (auto binaryOp = mlir::dyn_cast_or_null<BinaryOperator>(op)) {
            // TODO implement
        }
        return mlir::failure();
    }
};
class PrepareRelAlgLoweringPass : public mlir::PassWrapper<PrepareRelAlgLoweringPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gpm-prepare-relalg-lowering"; }

    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PrepareRelAlgLoweringPass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.insert<CreateInFlightOps>(&getContext());
        if (lingodb::compiler::applyPatternsGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
            signalPassFailure();
        }
   }
};

} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createPrepareRelAlgLoweringPass() { return std::make_unique<PrepareRelAlgLoweringPass>(); }