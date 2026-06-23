#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsTypes.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "lingodb/compiler/helper.h"

namespace {
using namespace gengodb::compiler::dialect;
using namespace lingodb::compiler::dialect;

inline static tuples::ColumnDefAttr createDef(tuples::ColumnManager& columnManager, std::string scope, std::string name, mlir::Type type, bool uniqueScope = true) {
   scope = uniqueScope ? columnManager.getUniqueScope(scope) : scope;
   auto def = columnManager.createDef(scope, name);
   def.getColumn().type = type;
   return def;
}
inline static tuples::ColumnRefAttr createRef(tuples::ColumnManager& columnManager, std::string scope, std::string name) {
   auto col = columnManager.get(scope, name);
   return columnManager.createRef(col.get());
}
static std::pair<tuples::ColumnDefAttr, tuples::ColumnRefAttr> createColumn(mlir::Type type, std::string scope, std::string name) {
   auto& columnManager = type.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto def = createDef(columnManager, scope, name, type);
   auto ref = createRef(columnManager, def.getName().getRootReference().str(), def.getName().getLeafReference().str());
   return {def, ref};
}

class StringifyMaterializedGraphRefs : public mlir::RewritePattern {
    public:
    StringifyMaterializedGraphRefs(mlir::MLIRContext* context)
        : RewritePattern(relalg::MaterializeOp::getOperationName(), 1, context) {}
    mlir::LogicalResult matchAndRewrite(mlir::Operation* op_, mlir::PatternRewriter& rewriter) const override {
        auto op = mlir::cast<relalg::MaterializeOp>(op_);
        auto& memberManager = rewriter.getContext()->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
        auto loc = op->getLoc();
        auto ctxt = rewriter.getContext();
        if (!mlir::isa<subop::LocalTableType>(op.getResult().getType()))
            return mlir::failure();
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointAfter(op);
        auto tableType = mlir::cast<subop::LocalTableType>(op.getResult().getType());
        auto stream = op.getRel();
        llvm::SmallVector<mlir::Attribute, 8> newColRefs;
        for (size_t i = 0; i < op.getCols().size(); i++) {
            auto colRef = mlir::cast<tuples::ColumnRefAttr>(op.getCols()[i]);
            if (!mlir::isa<gsubop::NodeRefType>(colRef.getColumn().type) 
                || mlir::isa<gsubop::EdgeRefType>(colRef.getColumn().type)
                || mlir::isa<gsubop::PropertyRefType>(colRef.getColumn().type)) {
                    newColRefs.push_back(colRef);
                    return mlir::failure();
            }
            auto tableMember = tableType.getMembers().getMembers()[i];
            auto [newColDef, newColRef] = createColumn(memberManager.getType(tableMember), "vars", "str");
            stream = rewriter.create<gsubop::GraphRefToStringOp>(loc, stream, colRef, newColDef);
            newColRefs.push_back(newColRef);
        }
        auto newOp = rewriter.create<relalg::MaterializeOp>(loc, tableType, stream, mlir::ArrayAttr::get(ctxt, newColRefs), op.getColumns());
        rewriter.replaceOp(op, newOp);
        return mlir::success();
    }
};
class StringifyMaterializedGraphRefsPass : public mlir::PassWrapper<StringifyMaterializedGraphRefsPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gpm-stringify-graph-refs"; }

    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StringifyMaterializedGraphRefsPass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.insert<StringifyMaterializedGraphRefs>(&getContext());
        if (lingodb::compiler::applyPatternsGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
            signalPassFailure();
        }
   }
};

} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createStringifyMaterializedGraphRefsPass() { return std::make_unique<StringifyMaterializedGraphRefsPass>(); }