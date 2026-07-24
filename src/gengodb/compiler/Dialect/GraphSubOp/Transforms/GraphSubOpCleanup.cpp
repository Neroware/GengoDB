#include "gengodb/compiler/Dialect/GraphSubOp/Transforms/Passes.h"

#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOpsTypes.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

namespace {
using namespace gengodb::compiler::dialect;
using namespace lingodb::compiler::dialect;
using DefMappingCollector = llvm::SmallVector<subop::DefMappingPairT>;
using RefMappingCollector = llvm::SmallVector<subop::RefMappingPairT>;
using MemberMapper = llvm::DenseMap<subop::Member, subop::Member>;

static bool isGraphRefType(mlir::Type t) {
   return mlir::isa<gsubop::NodeRefType>(t) || mlir::isa<gsubop::EdgeRefType>(t);
}
static bool isNullableGraphRefType(mlir::Type refType) {
   auto nullable = mlir::dyn_cast<db::NullableType>(refType);
   if (!nullable) return false;
   auto inner = nullable.getType();
   return mlir::isa<gsubop::NodeRefType>(inner) || mlir::isa<gsubop::EdgeRefType>(inner);
}
static subop::Member idMemberOf(mlir::Type graphRefType) {
   if (auto nodeRef = mlir::dyn_cast<gsubop::NodeRefType>(graphRefType))
      return nodeRef.getNodeMembers().getMembers()[0];
   auto edgeRef = mlir::cast<gsubop::EdgeRefType>(graphRefType);
   return edgeRef.getEdgeMembers().getMembers()[0];
}
static std::pair<tuples::ColumnDefAttr, tuples::ColumnRefAttr> createColumn(mlir::MLIRContext* ctxt, mlir::Type type, std::string scope, std::string name) {
   auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto def = columnManager.createDef(columnManager.getUniqueScope(scope), name);
   def.getColumn().type = type;
   auto ref = columnManager.createRef(def.getColumnPtr().get());
   return {def, ref};
}
static mlir::Block* buildI32EqFn(mlir::OpBuilder& builder, mlir::Location loc, size_t count) {
   auto* block = new mlir::Block;
   mlir::OpBuilder::InsertionGuard guard(builder);
   builder.setInsertionPointToStart(block);
   llvm::SmallVector<mlir::Value> leftArgs, rightArgs;
   for (size_t i = 0; i < count; i++) leftArgs.push_back(block->addArgument(builder.getI32Type(), loc));
   for (size_t i = 0; i < count; i++) rightArgs.push_back(block->addArgument(builder.getI32Type(), loc));
   llvm::SmallVector<mlir::Value> cmps;
   for (size_t i = 0; i < count; i++) {
      cmps.push_back(builder.create<db::CmpOp>(loc, db::DBCmpPredicate::eq, leftArgs[i], rightArgs[i]));
   }
   mlir::Value anded = cmps.size() == 1 ? cmps[0] : builder.create<db::AndOp>(loc, cmps).getResult();
   builder.create<tuples::ReturnOp>(loc, anded);
   return block;
}

// Replace db ops on !db.nullable<GraphRefType> with GSubOp ops
class NullableGraphRefCleanup {
   mlir::MLIRContext* ctxt;
   mlir::Location loc;
   public:
   NullableGraphRefCleanup(mlir::MLIRContext* ctxt, mlir::Location loc)
      : ctxt(ctxt), loc(loc) {}
   void apply(db::NullOp nullOp) const {
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(nullOp);
      auto newOp = builder.create<gsubop::NullRefOp>(loc, nullOp.getType());
      nullOp.replaceAllUsesWith(newOp.getResult());
      nullOp.erase();
   }
   void apply(db::AsNullableOp asNullableOp) const {
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(asNullableOp);
      auto newOp = builder.create<gsubop::WrapNullableRefOp>(loc, asNullableOp.getResult().getType(), asNullableOp.getVal());
      asNullableOp.replaceAllUsesWith(newOp.getResult());
      asNullableOp.erase();
   }
};

class GraphSubOpCleanupPass : public mlir::PassWrapper<GraphSubOpCleanupPass, mlir::OperationPass<mlir::ModuleOp>> {
   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GraphSubOpCleanupPass)
   llvm::StringRef getArgument() const override { return "gsubop-cleanup"; }
   void runOnOperation() override {
      auto processNullableOps = [&]<typename OpTy>() {
         llvm::SmallVector<OpTy> toProcess;
         getOperation()->walk([&](OpTy op) {
            if (!isNullableGraphRefType(op.getResult().getType()))
               return;
            toProcess.push_back(op);
         });
         NullableGraphRefCleanup cleanup(&getContext(), getOperation().getLoc());
         for (auto op : toProcess) {
            cleanup.apply(op);
         }
      };
      processNullableOps.template operator()<db::NullOp>();
      processNullableOps.template operator()<db::AsNullableOp>();
   }
};
} // namespace

std::unique_ptr<mlir::Pass> gsubop::createGraphSubOpCleanupPass() { return std::make_unique<GraphSubOpCleanupPass>(); }
