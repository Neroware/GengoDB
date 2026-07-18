#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsTypes.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/IR/IRMapping.h"
#include "lingodb/compiler/helper.h"

namespace {
using namespace gengodb::compiler::dialect;
using namespace lingodb::compiler::dialect;

class CreateInFlightOps : public mlir::RewritePattern {
    public:
    CreateInFlightOps(mlir::MLIRContext* context)
        : RewritePattern(MatchAnyOpTypeTag(), 1, context) {}
    mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
        auto isRelalg = [](const mlir::Operation* op) -> bool {
            return op && !mlir::isa<GPMOperator>(op) && mlir::isa<Operator>(op);
        };
        if (!isRelalg(op) || mlir::isa<relalg::InFlightOp>(op)) return mlir::failure();
        relalg::AvailabilityCache availabilityCache;
        auto computeColumns = [&](mlir::Operation* producer) -> mlir::ArrayAttr {
            if (auto producerOperator = mlir::dyn_cast_or_null<Operator>(producer)) {
                return producerOperator.getAvailableColumns(availabilityCache).asRefArrayAttr(rewriter.getContext());
            }
            return rewriter.getArrayAttr({});
        };
        mlir::IRMapping mapper;
        if (auto unaryOp = mlir::dyn_cast_or_null<UnaryOperator>(op)) {
            auto producer = unaryOp->getOperand(0).getDefiningOp();
            if (isRelalg(producer))
                return mlir::failure();
            auto inFlight = rewriter.create<relalg::InFlightOp>(op->getLoc(), unaryOp->getOperand(0), computeColumns(producer));
            mapper.map(op->getOperand(0), inFlight.getRes());
        }
        else if (auto binaryOp = mlir::dyn_cast_or_null<BinaryOperator>(op)) {
            auto left = binaryOp->getOperand(0).getDefiningOp();
            auto right = binaryOp->getOperand(1).getDefiningOp();
            bool leftRelalg = isRelalg(left);
            bool rightRelalg = isRelalg(right);
            if (leftRelalg && rightRelalg) return mlir::failure();
            if (!leftRelalg) {
                auto inFlight = rewriter.create<relalg::InFlightOp>(op->getLoc(), binaryOp->getOperand(0), computeColumns(left));
                mapper.map(op->getOperand(0), inFlight.getRes());
            }
            if (!rightRelalg) {
                auto inFlight = rewriter.create<relalg::InFlightOp>(op->getLoc(), binaryOp->getOperand(1), computeColumns(right));
                mapper.map(op->getOperand(1), inFlight.getRes());
            }
        }
        else {
            return mlir::failure();
        }
        auto newOp = rewriter.clone(*op, mapper);
        rewriter.replaceOp(op, newOp);
        return mlir::success();
    }
};
class CreateRelAlgInFlightsPass : public mlir::PassWrapper<CreateRelAlgInFlightsPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gpm-create-relalg-inflights"; }

    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CreateRelAlgInFlightsPass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.insert<CreateInFlightOps>(&getContext());
        if (lingodb::compiler::applyPatternsGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
            signalPassFailure();
        }
   }
};

} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createCreateRelAlgInFlightsPass() { return std::make_unique<CreateRelAlgInFlightsPass>(); }