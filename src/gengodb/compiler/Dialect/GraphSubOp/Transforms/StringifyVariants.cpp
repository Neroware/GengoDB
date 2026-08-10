#include "gengodb/compiler/Dialect/GraphSubOp/Transforms/Passes.h"

#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsTypes.h"
#include "lingodb/compiler/Dialect/SubOperator/Utils.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"
#include "gengodb/compiler/Dialect/Variant/VariantOps.h"
#include "gengodb/compiler/Dialect/Variant/VariantOpsEnums.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
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

static inline bool isVariant(mlir::Type type) {
    return mlir::isa<variant::VariantType>(type);
}
static inline bool isNullableVariant(mlir::Type type) {
    auto nullable = mlir::dyn_cast<db::NullableType>(type);
    return nullable && (mlir::isa<variant::VariantType>(nullable.getType()));
}
static inline bool isVariantOrNullableVariant(mlir::Type type) {
    return isVariant(type) || isNullableVariant(type);
}
static inline bool isNullableDBString(mlir::Type type) {
    auto nullable = mlir::dyn_cast<db::NullableType>(type);
    return nullable && mlir::isa<db::StringType>(nullable.getType());
}
static inline bool isDBStringOrNullableDBString(mlir::Type type) {
    return mlir::isa<db::StringType>(type) || isNullableDBString(type);
}

static mlir::Value buildToStringExpr(mlir::OpBuilder& b, mlir::Location loc, mlir::Value val, mlir::Type strType) {
    auto ctxt = b.getContext();
    auto dbStringType = db::StringType::get(ctxt);
    mlir::Value raw;
    mlir::Value isNull;
    if (mlir::isa<db::NullableType>(val.getType())) {
        isNull = b.create<db::IsNullOp>(loc, val);
        auto ifOp = b.create<mlir::scf::IfOp>(
           loc, isNull,
           [&](mlir::OpBuilder& b2, mlir::Location l) {
              mlir::Value placeholder = b2.create<db::ConstantOp>(l, dbStringType, b2.getStringAttr(""));
              b2.create<mlir::scf::YieldOp>(l, placeholder);
           },
           [&](mlir::OpBuilder& b2, mlir::Location l) {
              auto nonNull = b2.create<db::NullableGetVal>(l, val);
              mlir::Value str = b2.create<variant::ToStringOp>(l, dbStringType, nonNull);
              str.getDefiningOp()->setAttr("full", mlir::UnitAttr::get(ctxt));
              b2.create<mlir::scf::YieldOp>(l, str);
           });
        raw = ifOp.getResult(0);
    } else {
        raw = b.create<variant::ToStringOp>(loc, dbStringType, val);
        raw.getDefiningOp()->setAttr("full", mlir::UnitAttr::get(ctxt));
    }
    if (!mlir::isa<db::NullableType>(strType))
        return raw;
    if (isNull)
        return b.create<db::AsNullableOp>(loc, strType, raw, isNull);
    return b.create<db::AsNullableOp>(loc, strType, raw);
}

class StringifyMaterializedVariants : public mlir::OpRewritePattern<subop::MaterializeOp> {
    public:
    using mlir::OpRewritePattern<subop::MaterializeOp>::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(subop::MaterializeOp op, mlir::PatternRewriter& rewriter) const override {
        if (!mlir::isa<subop::ResultTableType>(op.getState().getType()))
            return mlir::failure();
        auto ctxt = rewriter.getContext();
        auto& memberManager = ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
        auto mapping = op.getMapping();
        auto needsStringify = [&](const subop::RefMappingPairT& pair) {
            return isVariantOrNullableVariant(pair.second.getColumn().type) && isDBStringOrNullableDBString(memberManager.getType(pair.first));
        };
        if (llvm::none_of(mapping.getMapping(), needsStringify))
            return mlir::failure();

        auto loc = op->getLoc();
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(op);

        subop::MapCreationHelper helper(ctxt);
        llvm::SmallVector<mlir::Attribute> computedCols;
        llvm::SmallVector<subop::RefMappingPairT> newMapping;
        helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
            llvm::SmallVector<mlir::Value> computedVals;
            for (auto& pair : mapping.getMapping()) {
                if (!needsStringify(pair)) {
                    newMapping.push_back(pair);
                    continue;
                }
                auto strType = memberManager.getType(pair.first);
                if (isNullableVariant(pair.second.getColumn().type) && !mlir::isa<db::NullableType>(strType)) {
                    strType = db::NullableType::get(ctxt, strType);
                    pair.first.internal->type = strType;
                }
                auto [newColDef, newColRef] = createColumn(strType, "vars", "str");
                auto val = helper.access(pair.second, loc);
                computedVals.push_back(buildToStringExpr(b, loc, val, strType));
                computedCols.push_back(newColDef);
                newMapping.push_back({pair.first, newColRef});
            }
            b.create<tuples::ReturnOp>(loc, computedVals);
        });
        auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), op.getStream(), rewriter.getArrayAttr(computedCols), helper.getColRefs());
        mapOp.getFn().push_back(helper.getMapBlock());

        rewriter.replaceOpWithNewOp<subop::MaterializeOp>(op, mapOp.getResult(), op.getState(), subop::ColumnRefMemberMappingAttr::get(ctxt, newMapping));
        return mlir::success();
    }
};
class StringifyVariantsPass : public mlir::PassWrapper<StringifyVariantsPass, mlir::OperationPass<mlir::ModuleOp>> {
    virtual llvm::StringRef getArgument() const override { return "gsubop-stringify-variants"; }

    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StringifyVariantsPass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.insert<StringifyMaterializedVariants>(&getContext());
        if (lingodb::compiler::applyPatternsGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
            signalPassFailure();
        }
   }
};

} // end anonymous namespace

std::unique_ptr<mlir::Pass> gsubop::createStringifyVariantsPass() { return std::make_unique<StringifyVariantsPass>(); }
