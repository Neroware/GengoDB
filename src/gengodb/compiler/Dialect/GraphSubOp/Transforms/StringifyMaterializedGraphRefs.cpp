#include "gengodb/compiler/Dialect/GraphSubOp/Transforms/Passes.h"

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

static bool isGraphRefType(mlir::Type type) {
   return mlir::isa<gsubop::NodeRefType>(type) || mlir::isa<gsubop::EdgeRefType>(type) || mlir::isa<gsubop::PropertyRefType>(type);
}

class StringifyMaterializedGraphRefs : public mlir::OpRewritePattern<subop::MaterializeOp> {
    public:
    using mlir::OpRewritePattern<subop::MaterializeOp>::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(subop::MaterializeOp op, mlir::PatternRewriter& rewriter) const override {
        auto& memberManager = rewriter.getContext()->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
        if (!mlir::isa<subop::ResultTableType>(op.getState().getType()))
            return mlir::failure();
        auto mapping = op.getMapping();
        if (llvm::none_of(mapping.getMapping(), [](const subop::RefMappingPairT& pair) { return isGraphRefType(pair.second.getColumn().type); }))
            return mlir::failure();

        auto loc = op->getLoc();
        auto ctxt = rewriter.getContext();
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(op);
        auto stream = op.getStream();
        llvm::SmallVector<subop::RefMappingPairT> newMapping;
        for (auto& pair : mapping.getMapping()) {
            auto colRef = pair.second;
            if (!isGraphRefType(colRef.getColumn().type)) {
                newMapping.push_back(pair);
                continue;
            }
            auto [newColDef, newColRef] = createColumn(memberManager.getType(pair.first), "vars", "str");
            stream = rewriter.create<gsubop::GraphRefToStringOp>(loc, stream, colRef, newColDef);
            newMapping.push_back({pair.first, newColRef});
        }
        rewriter.replaceOpWithNewOp<subop::MaterializeOp>(op, stream, op.getState(), subop::ColumnRefMemberMappingAttr::get(ctxt, newMapping));
        return mlir::success();
    }
};
class StringifyMaterializedGraphRefsPass : public mlir::PassWrapper<StringifyMaterializedGraphRefsPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gsubop-stringify-graph-refs"; }

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

std::unique_ptr<mlir::Pass> gsubop::createStringifyMaterializedGraphRefsPass() { return std::make_unique<StringifyMaterializedGraphRefsPass>(); }
