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

class PrepareHashJoins : public mlir::OpRewritePattern<relalg::InnerJoinOp> {
    public:
    using mlir::OpRewritePattern<relalg::InnerJoinOp>::OpRewritePattern;

    static bool needsReduction(mlir::ArrayAttr hashAttr) {
        if (!hashAttr) return false;
        return llvm::any_of(hashAttr, [](mlir::Attribute attr) {
            auto columnRef = mlir::dyn_cast_or_null<tuples::ColumnRefAttr>(attr);
            return columnRef && mlir::isa<gsubop::NodeRefType>(columnRef.getColumn().type);
        });
    }
    mlir::LogicalResult matchAndRewrite(relalg::InnerJoinOp joinOp, mlir::PatternRewriter& rewriter) const override {
        auto implAttr = joinOp->getAttrOfType<mlir::StringAttr>("impl");
        if (!implAttr || implAttr.getValue() != "hash") return mlir::failure();
        auto leftHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("leftHash");
        auto rightHashAttr = joinOp->getAttrOfType<mlir::ArrayAttr>("rightHash");
        bool leftNeeds = needsReduction(leftHashAttr);
        bool rightNeeds = needsReduction(rightHashAttr);
        if (!leftNeeds && !rightNeeds) return mlir::failure();

        auto ctxt = rewriter.getContext();
        auto loc = joinOp->getLoc();
        auto& colManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
        rewriter.setInsertionPoint(joinOp);
        auto reduceHashInputs = [&](mlir::Value operand, mlir::ArrayAttr hashSide, std::vector<mlir::Attribute>& refs) -> mlir::Value {
            mlir::Value stream = operand;
            relalg::ColumnSet availableColumns;
            if (auto originalOperator = mlir::dyn_cast_or_null<Operator>(operand.getDefiningOp())) {
                relalg::AvailabilityCache cache;
                availableColumns.insert(originalOperator.getAvailableColumns(cache));
            }
            bool reducedAny = false;
            for (const auto& attr : hashSide) {
                auto columnRef = mlir::dyn_cast_or_null<tuples::ColumnRefAttr>(attr);
                auto nodeRefType = columnRef ? mlir::dyn_cast_or_null<gsubop::NodeRefType>(columnRef.getColumn().type) : nullptr;
                if (!nodeRefType) {
                    refs.push_back(attr);
                    continue;
                }
                reducedAny = true;
                auto nodeIdValDefAttr = colManager.createDef(colManager.getUniqueScope("hashjoin"), "var");
                nodeIdValDefAttr.getColumn().type = mlir::IntegerType::get(ctxt, 32);
                stream = rewriter.create<subop::GatherOp>(loc, stream, columnRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getNodeMembers().getMembers()[0], nodeIdValDefAttr}}));
                auto newRef = colManager.createRef(nodeIdValDefAttr.getColumnPtr().get());
                refs.push_back(newRef);
                availableColumns.insert(&newRef.getColumn());
            }
            if (reducedAny) {
                stream = rewriter.create<relalg::InFlightOp>(loc, stream, availableColumns.asRefArrayAttr(ctxt));
            }
            return stream;
        };

        mlir::Value newLeft = joinOp.getLeft();
        mlir::Value newRight = joinOp.getRight();
        std::vector<mlir::Attribute> newLeftHash, newRightHash;
        if (leftNeeds) newLeft = reduceHashInputs(joinOp.getLeft(), leftHashAttr, newLeftHash);
        if (rightNeeds) newRight = reduceHashInputs(joinOp.getRight(), rightHashAttr, newRightHash);

        rewriter.modifyOpInPlace(joinOp, [&]() {
            if (leftNeeds) {
                joinOp.getLeftMutable().assign(newLeft);
                joinOp->setAttr("leftHash", mlir::ArrayAttr::get(ctxt, newLeftHash));
            }
            if (rightNeeds) {
                joinOp.getRightMutable().assign(newRight);
                joinOp->setAttr("rightHash", mlir::ArrayAttr::get(ctxt, newRightHash));
            }
        });
        return mlir::success();
    }
};
class PrepareRelAlgLoweringPass : public mlir::PassWrapper<PrepareRelAlgLoweringPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gpm-prepare-relalg-lowering"; }

    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PrepareRelAlgLoweringPass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.insert<PrepareHashJoins>(&getContext());
        if (lingodb::compiler::applyPatternsGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
            signalPassFailure();
        }
   }
};

} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createPrepareRelAlgLoweringPass() { return std::make_unique<PrepareRelAlgLoweringPass>(); }