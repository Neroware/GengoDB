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
using DefMappingCollector = llvm::SmallVector<subop::DefMappingPairT>;

static subop::ColumnDefMemberMappingAttr createColumnDefMemberMappingAttr(mlir::MLIRContext* context, DefMappingCollector pairs) {
   return subop::ColumnDefMemberMappingAttr::get(context, pairs);
}

class CreateInFlightOps : public mlir::RewritePattern {
    public:
    CreateInFlightOps(mlir::MLIRContext* context)
        : RewritePattern(MatchAnyOpTypeTag(), 1, context) {}
    mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
        if (mlir::isa<relalg::InFlightOp>(op) || mlir::isa<GPMOperator>(op) 
            || !mlir::isa<Operator>(op)) return mlir::failure();
        mlir::IRMapping mapper;
        if (auto unaryOp = mlir::dyn_cast_or_null<UnaryOperator>(op)) {
            auto subOp = unaryOp->getOperand(0).getDefiningOp();
            if (!mlir::isa<subop::SubOperator>(subOp))
                return mlir::failure();
            auto inFlight = rewriter.create<relalg::InFlightOp>(op->getLoc(), unaryOp->getOperand(0));
            mapper.map(op->getOperand(0), inFlight.getRes());
        }
        else if (auto binaryOp = mlir::dyn_cast_or_null<BinaryOperator>(op)) {
            auto left = binaryOp->getOperand(0).getDefiningOp();
            auto right = binaryOp->getOperand(1).getDefiningOp();
            if (!mlir::isa<subop::SubOperator>(left) && !mlir::isa<subop::SubOperator>(right))
                return mlir::failure();
            if (mlir::isa<subop::SubOperator>(left)) {
                auto inFlight = rewriter.create<relalg::InFlightOp>(op->getLoc(), binaryOp->getOperand(0));
                mapper.map(op->getOperand(0), inFlight.getRes());
            }
            if (mlir::isa<subop::SubOperator>(right)) {
                auto inFlight = rewriter.create<relalg::InFlightOp>(op->getLoc(), binaryOp->getOperand(1));
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
class PrepareHashJoins : public mlir::RewritePattern {
    public:
    PrepareHashJoins(mlir::MLIRContext* context)
        : RewritePattern(relalg::InFlightOp::getOperationName(), 1, context) {}
    mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
        auto userOp = *(op->getResult(0).getUsers().begin());
        if (!mlir::isa<relalg::InnerJoinOp>(userOp)) 
            return mlir::failure();
        auto joinOp = mlir::cast<relalg::InnerJoinOp>(userOp);
        if (auto implAttr = joinOp->getAttrOfType<mlir::StringAttr>("impl")) {
            if (implAttr.getValue() != "hash") return mlir::failure();
        }
        auto leftHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("leftHash");
        auto rightHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("rightHash");
        // TODO implement
        // if (!mlir::isa<gsubop::NodeRefType>(leftHash.getColumn().type) && !mlir::isa<gsubop::NodeRefType>(rightHash.getColumn().type))
        //     return mlir::failure();
        
        // auto ctxt = rewriter.getContext();
        // auto loc = op->getLoc();
        // auto& colManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
        // mlir::IRMapping mapper;
        // if (auto nodeRefType = mlir::dyn_cast_or_null<gsubop::NodeRefType>(leftHash.getColumn().type)) {
        //     auto nodeIdValDefAttr = colManager.createDef(colManager.getUniqueScope("hashjoin"), "left");
        //     auto nodeId = rewriter.create<subop::GatherOp>(loc, joinOp.getLeft(), leftHash, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeRefType.getNodeMembers().getMembers()[0], nodeIdValDefAttr}}));
        //     mapper.map(joinOp.getLeft(), nodeId);
        // }
        // if (auto nodeRefType = mlir::dyn_cast_or_null<gsubop::NodeRefType>(rightHash.getColumn().type)) {
        //     auto nodeIdValDefAttr = colManager.createDef(colManager.getUniqueScope("hashjoin"), "right");
        //     auto nodeId = rewriter.create<subop::GatherOp>(loc, joinOp.getRight(), rightHash, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeRefType.getNodeMembers().getMembers()[0], nodeIdValDefAttr}}));
        //     mapper.map(joinOp.getRight(), nodeId);
        // }
        // auto newOp = rewriter.clone(*op, mapper);
        // rewriter.replaceOp(op, newOp);
        return mlir::success();
    }
};
class PrepareRelAlgLoweringPass : public mlir::PassWrapper<PrepareRelAlgLoweringPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gpm-prepare-relalg-lowering"; }

    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PrepareRelAlgLoweringPass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.insert<CreateInFlightOps>(&getContext());
        patterns.insert<PrepareHashJoins>(&getContext());
        if (lingodb::compiler::applyPatternsGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
            signalPassFailure();
        }
   }
};

} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createPrepareRelAlgLoweringPass() { return std::make_unique<PrepareRelAlgLoweringPass>(); }