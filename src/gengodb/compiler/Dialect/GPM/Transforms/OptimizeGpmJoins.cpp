#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"

namespace {
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;

namespace {

bool isNormalized(gpm::TriplePatternOp triple) {
   return llvm::all_of(mlir::ArrayRef<mlir::Attribute>{triple.getS(), triple.getP(), triple.getO()}, [](mlir::Attribute term) {
      if (mlir::isa<gpm::IdentifierTermAttr>(term)) return true;
      auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term);
      return varTerm && varTerm.getProducedBinding();
   });
}
bool hasAnchor(gpm::TriplePatternOp triple, llvm::ArrayRef<mlir::Attribute> indexKeys) {
   return llvm::any_of(mlir::ArrayRef<mlir::Attribute>{triple.getS(), triple.getO()}, [&](mlir::Attribute term) {
      auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term);
      if (!varTerm || !varTerm.getProducedBinding()) return false;
      return llvm::any_of(indexKeys, [&](mlir::Attribute key) {
         return &mlir::cast<tuples::ColumnRefAttr>(key).getColumn() == &varTerm.getProducedBinding().getColumn();
      });
   });
}
gpm::TriplePatternOp findIndexTriple(mlir::Value stream, mlir::Block* block) {
   while (auto* op = stream.getDefiningOp()) {
      if (!stream.hasOneUse() || op->getBlock() != block) 
         return {};
      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op)) 
         return triple;
      if (!mlir::isa<relalg::SelectionOp, relalg::MapOp>(op)) 
         return {};
      stream = op->getOperand(0);
   }
   return {};
}

} // end anonymous namespace

class OptimizeGpmJoinsPass : public mlir::PassWrapper<OptimizeGpmJoinsPass, mlir::OperationPass<mlir::func::FuncOp>> {
   using join_keys_t = std::pair<llvm::SmallVector<mlir::Attribute>, llvm::SmallVector<mlir::Attribute>>;
   virtual llvm::StringRef getArgument() const override { return "gpm-optimize-joins"; }
   virtual llvm::StringRef getDescription() const override { return "optimize gpm binding compatible joins"; }

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OptimizeGpmJoinsPass)

   void runOnOperation() override {
      relalg::AvailabilityCache cache;
      getOperation().walk([&](mlir::Operation* op) {
         if (mlir::isa<relalg::InnerJoinOp>(op)) {
            optimize(op, /*allowLeftIndex=*/true, cache);
         } else if (mlir::isa<relalg::OuterJoinOp>(op)) {
            // the preserved (left) side of an outer join is always the probe side
            optimize(op, /*allowLeftIndex=*/false, cache);
         }
      });
   }

   private:
   static void optimize(mlir::Operation* join, bool allowLeftIndex, relalg::AvailabilityCache& cache) {
      if (join->hasAttr("impl")) return;
      auto allKeys = getCompatibleJoinKeys(join, cache, mergedProbeColumns(join));
      auto* block = join->getBlock();
      // index side keys must be non-nullable (a triple always binds), probe side keys may be unbound
      auto keys = withoutNullableIndexKeys(allKeys, /*indexIsRight=*/true);
      gpm::TriplePatternOp triple = keys.first.empty() ? gpm::TriplePatternOp() : getIndexTriple(join->getOperand(1), anchorCandidates(keys, true), block);
      if (!triple && allowLeftIndex) {
         keys = withoutNullableIndexKeys(allKeys, /*indexIsRight=*/false);
         if (!keys.first.empty()) triple = getIndexTriple(join->getOperand(0), anchorCandidates(keys, false), block);
      }
      if (!triple) return;
      mlir::Builder builder(join->getContext());
      auto flags = [&](int64_t value) {
         return builder.getArrayAttr(llvm::SmallVector<mlir::Attribute>(keys.first.size(), builder.getI8IntegerAttr(value)));
      };
      join->setAttr("impl", builder.getStringAttr("graphIndex"));
      join->setAttr("useGraphIndex", builder.getUnitAttr());
      join->setAttr("graph", triple.getGraphRefAttr());
      join->setAttr("leftHash", builder.getArrayAttr(keys.first));
      join->setAttr("rightHash", builder.getArrayAttr(keys.second));
      join->setAttr("nullsEqual", flags(0));
      join->setAttr("bindingCompatible", flags(1));
      join->removeAttr("useHashJoin");
      join->removeAttr("reverseSides");
      triple->setAttr("virtual", builder.getUnitAttr());
   }
   // anchor candidates: index keys whose probe side partner is always bound
   // (a NULL anchor would need a full triple scan per probe tuple, a hash join is preferable there)
   static llvm::SmallVector<mlir::Attribute> anchorCandidates(const join_keys_t& keys, bool indexIsRight) {
      llvm::SmallVector<mlir::Attribute> candidates;
      for (auto [leftKey, rightKey] : llvm::zip(keys.first, keys.second)) {
         auto probeKey = mlir::cast<tuples::ColumnRefAttr>(indexIsRight ? leftKey : rightKey);
         if (!mlir::isa<db::NullableType>(probeKey.getColumn().type)) candidates.push_back(indexIsRight ? rightKey : leftKey);
      }
      return candidates;
   }
   static join_keys_t withoutNullableIndexKeys(const join_keys_t& keys, bool indexIsRight) {
      join_keys_t filtered;
      for (auto [leftKey, rightKey] : llvm::zip(keys.first, keys.second)) {
         auto indexKey = mlir::cast<tuples::ColumnRefAttr>(indexIsRight ? rightKey : leftKey);
         if (mlir::isa<db::NullableType>(indexKey.getColumn().type)) continue;
         filtered.first.push_back(leftKey);
         filtered.second.push_back(rightKey);
      }
      return filtered;
   }
   static gpm::TriplePatternOp getIndexTriple(mlir::Value child, llvm::ArrayRef<mlir::Attribute> indexKeys, mlir::Block* block) {
      auto triple = findIndexTriple(child, block);
      if (!triple || triple->hasAttr("virtual")) 
         return {};
      if (triple.getTruthDegrAttr()) 
         return {};
      if (!mlir::isa_and_nonnull<gpm::NamedGraphOp>(triple.getRel().getDefiningOp())) 
         return {};
      if (!isNormalized(triple) || !hasAnchor(triple, indexKeys)) 
         return {};
      return triple;
   }
   // unbound (NULL) probe keys are only supported if the join fills them in afterwards, i.e. an outer join whose
   // gpm.merge coalesces the probe column with the (padded) index side column
   static llvm::SmallPtrSet<const tuples::Column*, 4> mergedProbeColumns(mlir::Operation* join) {
      llvm::SmallPtrSet<const tuples::Column*, 4> merged;
      auto mergedCols = join->getAttrOfType<mlir::ArrayAttr>(gpm::kMergedColumnsAttr);
      if (!mlir::isa<relalg::OuterJoinOp>(join) || !mergedCols) return merged;
      for (auto attr : mergedCols) {
         auto sources = mlir::cast<mlir::ArrayAttr>(mlir::cast<tuples::ColumnDefAttr>(attr).getFromExisting());
         merged.insert(&mlir::cast<tuples::ColumnRefAttr>(sources[0]).getColumn());
      }
      return merged;
   }
   static join_keys_t getCompatibleJoinKeys(mlir::Operation* join, relalg::AvailabilityCache& cache, const llvm::SmallPtrSet<const tuples::Column*, 4>& mergedProbeColumns) {
      join_keys_t keys;
      auto returnOp = mlir::dyn_cast<tuples::ReturnOp>(mlir::cast<PredicateOperator>(join).getPredicateBlock().getTerminator());
      if (!returnOp || returnOp.getNumOperands() != 1)
         return keys;
      auto leftColumns = mlir::cast<Operator>(join->getOperand(0).getDefiningOp()).getAvailableColumns(cache);
      auto rightColumns = mlir::cast<Operator>(join->getOperand(1).getDefiningOp()).getAvailableColumns(cache);

      llvm::SmallVector<mlir::Value> compatibleKeys;
      llvm::SmallVector<mlir::Value, 8> reversed;
      reversed.push_back(returnOp.getOperand(0));
      while(!reversed.empty()) {
         mlir::Value current = reversed.pop_back_val();
         if (auto andOp =
            mlir::dyn_cast_or_null<db::AndOp>(current.getDefiningOp())) {
            for (mlir::Value operand : llvm::reverse(andOp.getVals())) {
               reversed.push_back(operand);
            }
         } 
         else {
            compatibleKeys.push_back(current);
         }
      }
      for (mlir::Value key : compatibleKeys) {
         auto cmpOp = mlir::dyn_cast_or_null<relalg::CmpOpInterface>(key.getDefiningOp());
         if (!cmpOp || !cmpOp.isBindingCompatiblePred()) continue;
         auto left = mlir::dyn_cast_or_null<tuples::GetColumnOp>(cmpOp.getLeft().getDefiningOp());
         auto right = mlir::dyn_cast_or_null<tuples::GetColumnOp>(cmpOp.getRight().getDefiningOp());
         if (!left || !right) continue;
         auto* leftColumn = &left.getAttr().getColumn();
         auto* rightColumn = &right.getAttr().getColumn();
         auto isSupported = [&](const tuples::Column* probe, const tuples::Column* index) {
            if (mlir::isa<db::NullableType>(index->type)) return false;
            return !mlir::isa<db::NullableType>(probe->type) || mergedProbeColumns.contains(probe);
         };
         if (!isSupported(leftColumn, rightColumn) && !isSupported(rightColumn, leftColumn)) continue;
         if (leftColumns.contains(leftColumn) && rightColumns.contains(rightColumn)) {
            keys.first.push_back(left.getAttr());
            keys.second.push_back(right.getAttr());
         } 
         else if (leftColumns.contains(rightColumn) && rightColumns.contains(leftColumn)) {
            keys.first.push_back(right.getAttr());
            keys.second.push_back(left.getAttr());
         }
      }
      return keys;
   }
};
} // end anonymous namespace

std::unique_ptr<mlir::Pass> gpm::createOptimizeGpmJoinsPass() { return std::make_unique<OptimizeGpmJoinsPass>(); }
