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
        bool leftSide = op->getResult(0) == joinOp.getLeft();
        bool rightSide = !leftSide;
        auto pred = [](const mlir::Attribute& attr) { 
            if (auto columnRef = mlir::dyn_cast_or_null<tuples::ColumnRefAttr>(attr)) {
                return !mlir::isa<gsubop::NodeRefType>(columnRef.getColumn().type);
            }
            return true;
        };
        if (leftSide) {
            if (auto leftHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("leftHash")) {
                if (std::all_of(leftHashAttr.begin(), leftHashAttr.end(), pred))
                    return mlir::failure();
            }
        }
       else if (rightSide) {
            if (auto rightHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("rightHash")) {
                if (std::all_of(rightHashAttr.begin(), rightHashAttr.end(), pred))
                    return mlir::failure();
            }
        }
        auto ctxt = rewriter.getContext();
        auto loc = op->getLoc();
        auto& colManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
        auto mapHashInputs = [&](mlir::Value stream, const mlir::ArrayAttr& hashSide, std::vector<mlir::Attribute>& refs) -> mlir::Value {
            for (const auto& attr : hashSide) {
                if (!mlir::isa<tuples::ColumnRefAttr>(attr))
                    continue;
                auto columnRef = mlir::cast<tuples::ColumnRefAttr>(attr);
                if (!mlir::isa<gsubop::NodeRefType>(columnRef.getColumn().type))
                    continue;
                auto nodeRefType = mlir::dyn_cast_or_null<gsubop::NodeRefType>(columnRef.getColumn().type);
                auto nodeIdValDefAttr = colManager.createDef(colManager.getUniqueScope("hashjoin"), "var");
                nodeIdValDefAttr.getColumn().type = mlir::IntegerType::get(ctxt, 32);
                stream = rewriter.create<subop::GatherOp>(loc, stream, columnRef, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeRefType.getNodeMembers().getMembers()[0], nodeIdValDefAttr}}));
                refs.push_back(colManager.createRef(nodeIdValDefAttr.getColumnPtr().get()));
            }
            return stream;
        };
        mlir::IRMapping mapper, joinMapper;
        std::vector<mlir::Attribute> refs;
        mlir::Operation* newOp;
        if (leftSide) {
            auto leftHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("leftHash");
            mapper.map(op->getOperand(0), mapHashInputs(op->getOperand(0), leftHashAttr, refs));
            newOp = rewriter.clone(*op, mapper);
            joinMapper.map(op->getResult(0), newOp->getResult(0));
        }
        if (rightSide) {
            auto rightHasAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("rightHash");
            mapper.map(op->getOperand(0), mapHashInputs(op->getOperand(0), rightHasAttr, refs));
            newOp = rewriter.clone(*op, mapper);
            joinMapper.map(op->getResult(0), newOp->getResult(0));
        }
        rewriter.setInsertionPoint(joinOp);
        auto* newJoinOp = rewriter.clone(*joinOp, joinMapper);
        if (leftSide) {
            newJoinOp->setAttr("leftHash", mlir::ArrayAttr::get(ctxt, refs));
        }
        if (rightSide) {
            newJoinOp->setAttr("rightHash", mlir::ArrayAttr::get(ctxt, refs));
        }
        rewriter.replaceOp(joinOp, newJoinOp);
        rewriter.replaceOp(op, newOp);
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