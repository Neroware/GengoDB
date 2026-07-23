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

static bool isGraphRefType(mlir::Type t) {
   return mlir::isa<gsubop::NodeRefType>(t) || mlir::isa<gsubop::EdgeRefType>(t);
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

class MultiMapCleanup {
   mlir::MLIRContext* ctxt;
   mlir::Location loc;
   subop::MemberManager& memberManager;
   public:
   MultiMapCleanup(mlir::MLIRContext* ctxt, mlir::Location loc, subop::MemberManager& memberManager)
      : ctxt(ctxt), loc(loc), memberManager(memberManager) {}
   void apply(subop::GenericCreateOp createOp) {
      mlir::OpBuilder builder(ctxt);
      auto mmType = mlir::cast<subop::MultiMapType>(createOp.getRes().getType());
      auto oldKeyMembers = mmType.getKeyMembers().getMembers();
      auto oldValueMembers = mmType.getValueMembers().getMembers();
      llvm::SmallVector<subop::Member> newKeyMembers;
      llvm::SmallVector<subop::Member> newValueMembers(oldValueMembers.begin(), oldValueMembers.end());
      llvm::DenseMap<subop::Member, subop::Member> reducedKeyToId;
      for (auto m : oldKeyMembers) {
         if (isGraphRefType(memberManager.getType(m))) {
            auto idMember = memberManager.createMember("hashkey", builder.getI32Type());
            newKeyMembers.push_back(idMember);
            newValueMembers.push_back(m);
            reducedKeyToId[m] = idMember;
         } 
         else {
            newKeyMembers.push_back(m);
         }
      }
      if (reducedKeyToId.empty()) 
         return;
      auto newMultiMapType = subop::MultiMapType::get(ctxt, subop::StateMembersAttr::get(ctxt, newKeyMembers), subop::StateMembersAttr::get(ctxt, newValueMembers));
      subop::InsertOp insertOp;
      subop::LookupOp lookupOp;
      for (auto* user : createOp.getRes().getUsers()) {
         if (auto ins = mlir::dyn_cast<subop::InsertOp>(user)) insertOp = ins;
         if (auto lk = mlir::dyn_cast<subop::LookupOp>(user)) lookupOp = lk;
      }
      assert(insertOp && lookupOp && "expected exactly one insert and one lookup consuming this multimap (translateHJ's own invariant)");
      builder.setInsertionPoint(createOp);
      auto newCreateOp = builder.create<subop::GenericCreateOp>(loc, newMultiMapType);
      rewriteInsert(builder, insertOp, newCreateOp.getRes(), reducedKeyToId, oldKeyMembers.size());
      rewriteLookup(builder, lookupOp, newCreateOp.getRes(), newMultiMapType, reducedKeyToId, oldKeyMembers.size());
      createOp->erase();
   }
   private:
   void rewriteInsert(mlir::OpBuilder& builder, subop::InsertOp insertOp, mlir::Value newState, llvm::DenseMap<subop::Member, subop::Member>& reducedKeyToId, size_t numKeyMembers) {
      RefMappingCollector newMapping;
      builder.setInsertionPoint(insertOp);
      mlir::Value stream = insertOp.getStream();
      for (auto& pair : insertOp.getMapping().getMapping()) {
         newMapping.push_back(pair);
         auto it = reducedKeyToId.find(pair.first);
         if (it == reducedKeyToId.end()) continue;
         auto colRef = pair.second;
         auto idMember = idMemberOf(colRef.getColumn().type);
         auto [idDef, idRef] = createColumn(ctxt, memberManager.getType(it->second), "hashkey", "id");
         stream = builder.create<subop::GatherOp>(loc, stream, colRef, subop::ColumnDefMemberMappingAttr::get(ctxt, DefMappingCollector{{idMember, idDef}}));
         newMapping.push_back({it->second, idRef});
      }
      insertOp.getStreamMutable().assign(stream);
      insertOp.getStateMutable().assign(newState);
      insertOp->setAttr("mapping", subop::ColumnRefMemberMappingAttr::get(ctxt, newMapping));
      insertOp.getEqFn().front().erase();
      insertOp.getEqFn().push_back(buildI32EqFn(builder, loc, numKeyMembers));
   }
   void rewriteLookup(mlir::OpBuilder& builder, subop::LookupOp lookupOp, mlir::Value newState, subop::MultiMapType newMultiMapType, llvm::DenseMap<subop::Member, subop::Member>& reducedKeyToId, size_t numKeyMembers) {
      auto loc = lookupOp->getLoc();
      auto oldKeys = lookupOp.getKeys();
      llvm::SmallVector<mlir::Attribute> newKeys;
      builder.setInsertionPoint(lookupOp);
      mlir::Value stream = lookupOp.getStream();
      for (auto keyAttr : oldKeys) {
         auto colRef = mlir::cast<tuples::ColumnRefAttr>(keyAttr);
         if (isGraphRefType(colRef.getColumn().type)) {
            auto idMember = idMemberOf(colRef.getColumn().type);
            auto idType = builder.getI32Type();
            auto [idDef, idRef] = createColumn(ctxt, idType, "hashkey", "id");
            stream = builder.create<subop::GatherOp>(loc, stream, colRef, subop::ColumnDefMemberMappingAttr::get(ctxt, DefMappingCollector{{idMember, idDef}}));
            newKeys.push_back(idRef);
         } 
         else {
            newKeys.push_back(keyAttr);
         }
      }
      lookupOp.getStreamMutable().assign(stream);
      lookupOp.getStateMutable().assign(newState);
      lookupOp->setAttr("keys", mlir::ArrayAttr::get(ctxt, newKeys));
      lookupOp.getEqFn().front().erase();
      lookupOp.getEqFn().push_back(buildI32EqFn(builder, loc, numKeyMembers));
      auto newEntryRefType = subop::MultiMapEntryRefType::get(ctxt, newMultiMapType);
      auto newListType = subop::ListType::get(ctxt, newEntryRefType);
      lookupOp.getRef().getColumn().type = newListType;
   }
};

class GraphSubOpCleanupPass : public mlir::PassWrapper<GraphSubOpCleanupPass, mlir::OperationPass<mlir::ModuleOp>> {
   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GraphSubOpCleanupPass)
   llvm::StringRef getArgument() const override { return "gsubop-cleanup"; }
   void runOnOperation() override {
      auto& memberManager = getContext().getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
      llvm::SmallVector<subop::GenericCreateOp> toProcess;
      getOperation()->walk([&](subop::GenericCreateOp createOp) {
         auto mmType = mlir::dyn_cast<subop::MultiMapType>(createOp.getRes().getType());
         if (!mmType) return;
         bool needsReduction = llvm::any_of(mmType.getKeyMembers().getMembers(), [&](subop::Member m) {
            return isGraphRefType(memberManager.getType(m));
         });
         if (needsReduction) toProcess.push_back(createOp);
      });
      for (auto createOp : toProcess) {
         MultiMapCleanup(&getContext(), createOp->getLoc(), memberManager).apply(createOp);
      }
   }
};
} // namespace

std::unique_ptr<mlir::Pass> gsubop::createGraphSubOpCleanupPass() { return std::make_unique<GraphSubOpCleanupPass>(); }
