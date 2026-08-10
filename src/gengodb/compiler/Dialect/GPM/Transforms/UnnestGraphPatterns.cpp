#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"

namespace {
using namespace gengodb::compiler::dialect;
using namespace lingodb::compiler::dialect;
using ColumnMapper = llvm::DenseMap<const tuples::Column*, const tuples::Column*>;
const tuples::Column* getBNodeScopeColumn(mlir::Attribute entry) {
   if (auto def = mlir::dyn_cast<tuples::ColumnDefAttr>(entry)) 
      return &def.getColumn();
   return &mlir::cast<tuples::ColumnRefAttr>(entry).getColumn();
}
tuples::ColumnRefAttr getBNodeScopeRef(mlir::Attribute entry, tuples::ColumnManager& columnManager) {
   if (auto def = mlir::dyn_cast<tuples::ColumnDefAttr>(entry))
      return columnManager.createRef(def.getColumnPtr().get());
   return mlir::cast<tuples::ColumnRefAttr>(entry);
}
const tuples::Column* resolveThroughRemaps(const ColumnMapper& remaps, const tuples::Column* column) {
   for (auto it = remaps.find(column); it != remaps.end(); it = remaps.find(column)) {
      column = it->second;
   }
   return column;
}
bool isGpmStreamOp(mlir::Operation* op) {
   return op && (mlir::isa<GraphPatternOp>(op) || mlir::isa<gpm::BagOp, gpm::TriplePatternOp, gpm::NamedGraphOp>(op));
}
bool isNestedInGraphPattern(mlir::Operation* op) {
   for (auto* parent = op->getParentOp(); parent; parent = parent->getParentOp()) {
      if (mlir::isa<GraphPatternOp>(parent)) return true;
   }
   return false;
}

class UnnestGraphPatternsPass : public mlir::PassWrapper<UnnestGraphPatternsPass, mlir::OperationPass<mlir::ModuleOp>> {
   virtual llvm::StringRef getArgument() const override { return "gpm-unnest-patterns"; }

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnnestGraphPatternsPass)

   void runOnOperation() override {
      llvm::SmallVector<std::pair<mlir::Operation*, unsigned>> entries;
      getOperation()->walk([&](mlir::Operation* op) {
         if (isGpmStreamOp(op) || isNestedInGraphPattern(op)) return;
         for (auto [i, operand] : llvm::enumerate(op->getOperands())) {
            if (mlir::isa<tuples::TupleStreamType>(operand.getType()) && isGpmStreamOp(operand.getDefiningOp())) {
               entries.emplace_back(op, i);
            }
         }
      });
      for (auto [consumer, operandIdx] : entries) {
         insertPoint = consumer;
         if (auto rewrittenOperand = rewrite(consumer->getOperand(operandIdx))) {
            consumer->setOperand(operandIdx, rewrittenOperand);
         }
      }
      for (bool erasedAny = true; erasedAny;) {
         erasedAny = false;
         llvm::SmallVector<mlir::Operation*> dead;
         getOperation()->walk([&](mlir::Operation* op) {
            if (mlir::isa<gpm::NamedGraphOp, relalg::TmpOp>(op) && op->use_empty()) dead.push_back(op);
         });
         for (auto* op : dead) {
            op->erase();
            erasedAny = true;
         }
      }
   }

   private:
   llvm::DenseMap<mlir::Value, mlir::Value> rewritten;
   mlir::Operation* insertPoint = nullptr;
   llvm::StringMap<tuples::ColumnRefAttr>* bnodeScope = nullptr;
   mlir::Value rewrite(mlir::Value v) {
      if (!v) return v;
      if (auto it = rewritten.find(v); it != rewritten.end()) return it->second;
      auto* op = v.getDefiningOp();
      if (!op || mlir::isa<gpm::NamedGraphOp>(op)) return {};
      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op)) {
         auto result = rewriteTriple(triple);
         rewritten[v] = result;
         return result;
      }
      mlir::Value result;
      if (auto patternOp = mlir::dyn_cast<GraphPatternOp>(op)) {
         result = rewritePattern(patternOp);
      } 
      else if (auto unionOp = mlir::dyn_cast<gpm::BagOp>(op)) {
         result = rewriteUnion(unionOp);
      } 
      else {
         auto* savedInsertPoint = insertPoint;
         if (isNestedInGraphPattern(op)) op->moveBefore(insertPoint);
         insertPoint = op;
         bool readsStream = false;
         bool readsOnlySeeds = true;
         for (auto& operand : op->getOpOperands()) {
            if (!mlir::isa<tuples::TupleStreamType>(operand.get().getType())) continue;
            readsStream = true;
            if (auto rewrittenOperand = rewrite(operand.get())) {
               operand.set(rewrittenOperand);
               readsOnlySeeds = false;
            }
         }
         insertPoint = savedInsertPoint;
         result = readsStream && readsOnlySeeds ? mlir::Value() : v;
         rewritten[v] = result;
         return result;
      }
      if (!result) {
         op->emitOpError("produces no tuple stream to unnest");
         signalPassFailure();
         return result;
      }
      rewritten[v] = result;
      rewritten[result] = result;
      op->getResult(0).replaceAllUsesWith(result);
      op->erase();
      return result;
   }
   mlir::Value rewriteTriple(gpm::TriplePatternOp triple) {
      mlir::Value accumulator = rewrite(triple.getRel());
      llvm::StringMap<tuples::ColumnRefAttr> ownScope;
      annotateBNodeScope(triple, bnodeScope ? *bnodeScope : ownScope);
      isolateTriple(triple, insertPoint);
      return fold(accumulator, triple.getRes(), gpm::PatternKind::basic, relalg::ColumnSet(), triple.getLoc());
   }
   mlir::Value rewritePattern(GraphPatternOp patternOp) {
      auto createdVars = mlir::cast<GPMOperator>(patternOp.getOperation()).getCreatedVariables();
      auto kind = patternOp.getPatternKind();
      auto loc = patternOp.getLoc();
      mlir::Value accumulator = rewrite(patternOp.getRel());
      mlir::Value elementStream = rewriteRegion(patternOp.getPattern());
      ColumnMapper nestedRemaps;
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedRemaps;
      collectOuterJoinRemaps(elementStream, visitedRemaps, nestedRemaps);
      relalg::ColumnSet liveCreatedVars;
      for (const auto* column : createdVars) {
         liveCreatedVars.insert(resolveThroughRemaps(nestedRemaps, column));
      }
      return fold(accumulator, elementStream, kind, liveCreatedVars, loc);
   }
   mlir::Value rewriteRegion(mlir::Region& region) {
      auto* terminator = region.front().getTerminator();
      if (terminator->getNumOperands() == 0) return {};
      llvm::StringMap<tuples::ColumnRefAttr> scope;
      auto* savedScope = bnodeScope;
      bnodeScope = &scope;
      mlir::Value result = rewrite(terminator->getOperand(0));
      bnodeScope = savedScope;
      return result;
   }
   void collectOuterJoinRemaps(mlir::Value v, llvm::SmallPtrSet<mlir::Operation*, 16>& visited, ColumnMapper& remaps) {
      auto* op = v.getDefiningOp();
      if (!op || !visited.insert(op).second) return;
      if (auto outerJoin = mlir::dyn_cast<relalg::OuterJoinOp>(op)) {
         for (auto mappingAttr : outerJoin.getMapping()) {
            auto colDef = mlir::cast<tuples::ColumnDefAttr>(mappingAttr);
            if (auto fromExisting = mlir::dyn_cast_or_null<mlir::ArrayAttr>(colDef.getFromExisting())) {
               if (fromExisting.size() == 1) {
                  if (auto ref = mlir::dyn_cast<tuples::ColumnRefAttr>(fromExisting[0])) {
                     remaps[&ref.getColumn()] = &colDef.getColumn();
                  }
               }
            }
         }
      }
      if (auto unionOp = mlir::dyn_cast<relalg::UnionOp>(op)) {
         for (auto mappingAttr : unionOp.getMapping()) {
            auto colDef = mlir::cast<tuples::ColumnDefAttr>(mappingAttr);
            auto fromExisting = mlir::cast<mlir::ArrayAttr>(colDef.getFromExisting());
            for (auto entry : fromExisting) {
               if (auto ref = mlir::dyn_cast<tuples::ColumnRefAttr>(entry)) {
                  remaps[&ref.getColumn()] = &colDef.getColumn();
               }
            }
         }
      }
      for (auto operand : op->getOperands()) {
         collectOuterJoinRemaps(operand, visited, remaps);
      }
   }
   relalg::ColumnSet producedColumns(mlir::Value v) {
      llvm::SmallPtrSet<mlir::Operation*, 16> visited;
      return producedColumns(v, visited);
   }
   relalg::ColumnSet producedColumns(mlir::Value v, llvm::SmallPtrSetImpl<mlir::Operation*>& visited) {
      relalg::ColumnSet result;
      auto* op = v.getDefiningOp();
      if (!op || !visited.insert(op).second) return result;
      auto insertMapping = [&](mlir::ArrayAttr mapping) {
         for (auto attr : mapping) result.insert(mlir::cast<tuples::ColumnDefAttr>(attr).getColumnPtr().get());
      };
      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op)) {
         result.insert(triple.getCreatedColumns());
         return result;
      }
      if (auto unionOp = mlir::dyn_cast<relalg::UnionOp>(op)) {
         insertMapping(unionOp.getMapping());
         return result;
      }
      if (auto outerJoin = mlir::dyn_cast<relalg::OuterJoinOp>(op)) {
         insertMapping(outerJoin.getMapping());
         result.insert(producedColumns(outerJoin.getLeft(), visited));
         return result;
      }
      if (auto antiSemiJoin = mlir::dyn_cast<relalg::AntiSemiJoinOp>(op)) {
         result.insert(producedColumns(antiSemiJoin.getLeft(), visited));
         return result;
      }
      for (auto operand : op->getOperands()) {
         if (mlir::isa<tuples::TupleStreamType>(operand.getType())) {
            result.insert(producedColumns(operand, visited));
         }
      }
      return result;
   }
   mlir::Value rewriteUnion(gpm::BagOp bagOp) {
      auto* ctxt = bagOp.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto loc = bagOp.getLoc();

      mlir::Value leftStream = rewrite(bagOp.getLeft());
      mlir::Value rightStream = rewrite(bagOp.getRight());
      if (!leftStream || !rightStream) {
         bagOp.emitOpError("both branches must contain at least one pattern");
         signalPassFailure();
         return {};
      }
      ColumnMapper leftRemaps, rightRemaps;
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         collectOuterJoinRemaps(leftStream, visited, leftRemaps);
      }
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         collectOuterJoinRemaps(rightStream, visited, rightRemaps);
      }

      llvm::SmallVector<mlir::Attribute> mappingEntries;
      relalg::ColumnSet declared;
      for (auto attr : bagOp.getMapping()) {
         auto colDef = mlir::cast<tuples::ColumnDefAttr>(attr);
         auto fromExisting = mlir::cast<mlir::ArrayAttr>(colDef.getFromExisting());
         bool nullable = false;
         mlir::Type mergedType;
         auto resolveSide = [&](mlir::Attribute side, const ColumnMapper& remaps) -> mlir::Attribute {
            auto ref = mlir::dyn_cast<tuples::ColumnRefAttr>(side);
            if (!ref) {
               nullable = true;
               return side;
            }
            const auto* resolved = resolveThroughRemaps(remaps, &ref.getColumn());
            declared.insert(resolved);
            mergedType = resolved->type;
            if (auto nullableType = mlir::dyn_cast<db::NullableType>(mergedType)) {
               nullable = true;
               mergedType = nullableType.getType();
            }
            return resolved == &ref.getColumn() ? side : static_cast<mlir::Attribute>(columnManager.createRef(resolved));
         };
         auto leftEntry = resolveSide(fromExisting[0], leftRemaps);
         auto rightEntry = resolveSide(fromExisting[1], rightRemaps);
         auto newDef = columnManager.createDef(colDef.getColumnPtr().get(), mlir::ArrayAttr::get(ctxt, {leftEntry, rightEntry}));
         newDef.getColumn().type = nullable ? db::NullableType::get(ctxt, mergedType) : mergedType;
         mappingEntries.push_back(newDef);
      }

      relalg::ColumnSet leftProduced = producedColumns(leftStream);
      relalg::ColumnSet rightProduced = producedColumns(rightStream);

      auto passthroughScope = columnManager.getUniqueScope("union");
      ColumnMapper passthroughColMap;
      for (const auto* column : leftProduced) {
         if (!rightProduced.contains(column) || declared.contains(column)) continue;
         auto ref = columnManager.createRef(column);
         auto newDef = columnManager.createDef(passthroughScope, columnManager.getName(column).second, mlir::ArrayAttr::get(ctxt, {ref, ref}));
         newDef.getColumn().type = column->type;
         mappingEntries.push_back(newDef);
         passthroughColMap[column] = &newDef.getColumn();
      }
      llvm::MapVector<const tuples::Column*, std::pair<tuples::ColumnRefAttr, tuples::ColumnRefAttr>> bindingsByOuterCol;
      {
         auto pairs = ensureBindingsColumns(leftStream, loc, [&](const tuples::Column* c) { return !leftProduced.contains(c); });
         for (auto& pair : pairs) bindingsByOuterCol[&pair.outerRef.getColumn()].first = pair.bindingsRef;
      }
      {
         auto pairs = ensureBindingsColumns(rightStream, loc, [&](const tuples::Column* c) { return !rightProduced.contains(c); });
         for (auto& pair : pairs) bindingsByOuterCol[&pair.outerRef.getColumn()].second = pair.bindingsRef;
      }
      for (auto& [outerCol, sides] : bindingsByOuterCol) {
         auto leftBindingsRef = sides.first;
         auto rightBindingsRef = sides.second;
         auto bindingsScope = columnManager.getUniqueScope("union_bindings");
         auto name = columnManager.getName(outerCol).second;
         mlir::Attribute leftEntry = leftBindingsRef ? static_cast<mlir::Attribute>(leftBindingsRef) : static_cast<mlir::Attribute>(mlir::UnitAttr::get(ctxt));
         mlir::Attribute rightEntry = rightBindingsRef ? static_cast<mlir::Attribute>(rightBindingsRef) : static_cast<mlir::Attribute>(mlir::UnitAttr::get(ctxt));
         auto fromExisting = mlir::ArrayAttr::get(ctxt, {leftEntry, rightEntry});
         auto newDef = columnManager.createDef(bindingsScope, name, fromExisting);
         mlir::Type newType = (leftBindingsRef ? leftBindingsRef : rightBindingsRef).getColumn().type;
         if ((!leftBindingsRef || !rightBindingsRef) && !mlir::isa<db::NullableType>(newType)) {
            newType = db::NullableType::get(ctxt, newType);
         }
         newDef.getColumn().type = newType;
         mappingEntries.push_back(newDef);
      }

      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      auto relUnion = builder.create<relalg::UnionOp>(loc, relalg::SetSemanticAttr::get(ctxt, relalg::SetSemantic::all), leftStream, rightStream, mlir::ArrayAttr::get(ctxt, mappingEntries));
      llvm::SmallPtrSet<mlir::Operation*, 32> preMergeOps;
      collectSubtreeOps(leftStream, preMergeOps);
      collectSubtreeOps(rightStream, preMergeOps);
      remapColumnsEverywhere(relUnion.getOperation(), passthroughColMap, &preMergeOps);
      return relUnion.getResult();
   }
   void isolateTriple(gpm::TriplePatternOp triple, mlir::Operation* insertBefore) {
      auto* ctxt = triple.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto originalGraphType = triple.getGraphRef().getColumn().type;
      auto freshDef = columnManager.createDef(columnManager.getUniqueScope("graphs"), "ref");
      freshDef.getColumn().type = originalGraphType;
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertBefore);
      auto namedGraph = builder.create<gpm::NamedGraphOp>(triple.getLoc(), freshDef);
      triple.setGraphRefAttr(columnManager.createRef(freshDef.getColumnPtr().get()));
      triple.getRelMutable().set(namedGraph.getRes());
      triple->moveBefore(insertBefore);
   }
   mlir::Value fold(mlir::Value accumulator, mlir::Value elementStream, gpm::PatternKind elementKind, const relalg::ColumnSet& elementCreatedVars, mlir::Location loc) {
      if (!accumulator) return elementStream;
      mlir::OpBuilder builder(insertPoint->getContext());
      builder.setInsertionPoint(insertPoint);
      auto streamType = tuples::TupleStreamType::get(builder.getContext());
      if (elementKind == gpm::PatternKind::optional) {
         ColumnMapper nullableColMap;
         auto mapping = buildNullableMapping(builder, elementCreatedVars, nullableColMap);
         auto join = builder.create<relalg::OuterJoinOp>(loc, streamType, accumulator, elementStream, mapping);
         join.initPredicate();
         addSharedVariablePredicate(join, accumulator, elementStream, loc);
         llvm::SmallPtrSet<mlir::Operation*, 32> preMergeOps;
         collectSubtreeOps(accumulator, preMergeOps);
         collectSubtreeOps(elementStream, preMergeOps);
         remapColumnsEverywhere(join.getOperation(), nullableColMap, &preMergeOps);
         return join.getResult();
      }
      if (elementKind == gpm::PatternKind::minus) {
         auto join = builder.create<relalg::AntiSemiJoinOp>(loc, streamType, accumulator, elementStream);
         join.initPredicate();
         addSharedVariablePredicate(join, accumulator, elementStream, loc);
         return join.getResult();
      }
      auto join = builder.create<relalg::InnerJoinOp>(loc, streamType, accumulator, elementStream);
      join.initPredicate();
      addSharedVariablePredicate(join, accumulator, elementStream, loc);
      return join.getResult();
   }
   relalg::ColumnSet collectTripleVariables(mlir::Value v, llvm::SmallPtrSet<mlir::Operation*, 16>& visited) {
      relalg::ColumnSet result;
      auto* op = v.getDefiningOp();
      if (!op || !visited.insert(op).second) return result;
      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op)) {
         result.insert(triple.getCreatedVariables());
         result.insert(triple.getBoundVariables());
         if (auto bnodeScope = triple->getAttrOfType<mlir::DictionaryAttr>("bnodeScope")) {
            for (auto entry : bnodeScope) {
               result.insert(getBNodeScopeColumn(entry.getValue()));
            }
         }
      }
      if (auto outerJoin = mlir::dyn_cast<relalg::OuterJoinOp>(op)) {
         for (auto mappingAttr : outerJoin.getMapping()) {
            result.insert(&mlir::cast<tuples::ColumnDefAttr>(mappingAttr).getColumn());
         }
         result.insert(collectTripleVariables(outerJoin.getLeft(), visited));
         return result;
      }
      if (auto unionOp = mlir::dyn_cast<relalg::UnionOp>(op)) {
         for (auto mappingAttr : unionOp.getMapping()) {
            result.insert(&mlir::cast<tuples::ColumnDefAttr>(mappingAttr).getColumn());
         }
         result.insert(collectTripleVariables(unionOp.getLeft(), visited));
         result.insert(collectTripleVariables(unionOp.getRight(), visited));
         return result;
      }
      for (auto operand : op->getOperands()) {
         result.insert(collectTripleVariables(operand, visited));
      }
      return result;
   }
   void collectTriples(mlir::Value v, llvm::SmallPtrSet<mlir::Operation*, 16>& visited, llvm::SmallVectorImpl<gpm::TriplePatternOp>& result) {
      auto* op = v.getDefiningOp();
      if (!op || !visited.insert(op).second) return;
      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op)) {
         result.push_back(triple);
      }
      if (auto outerJoin = mlir::dyn_cast<relalg::OuterJoinOp>(op)) {
         collectTriples(outerJoin.getLeft(), visited, result);
         return;
      }
      for (auto operand : op->getOperands()) {
         collectTriples(operand, visited, result);
      }
   }
   struct BindingPair {
      tuples::ColumnRefAttr outerRef;
      tuples::ColumnRefAttr bindingsRef;
   };
   llvm::SmallVector<BindingPair> ensureBindingsColumns(mlir::Value right, mlir::Location loc, llvm::function_ref<bool(const tuples::Column*)> needsBindings) {
      auto* ctxt = right.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedRight;
      llvm::SmallVector<gpm::TriplePatternOp> rightTriples;
      collectTriples(right, visitedRight, rightTriples);

      llvm::SmallVector<BindingPair> result;
      for (auto triple : rightTriples) {
         auto existingBindings = triple->getAttrOfType<mlir::DictionaryAttr>("bindings");
         llvm::SmallVector<mlir::NamedAttribute> bindings;
         if (existingBindings) {
            for (auto namedAttr : existingBindings) bindings.push_back(namedAttr);
         }
         auto bnodeScope = triple->getAttrOfType<mlir::DictionaryAttr>("bnodeScope");
         for (auto [position, term] : {std::pair{"s", triple.getS()}, std::pair{"p", triple.getP()}, std::pair{"o", triple.getO()}}) {
            tuples::ColumnRefAttr outerRef;
            if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
               if (!varTerm.hasBinding()) continue;
               outerRef = varTerm.getBindingReference();
            }
            else if (auto bnodeTerm = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
               if (!bnodeScope) continue;
               auto entry = bnodeScope.get(bnodeTerm.getLocalId().getValue());
               if (!entry) continue;
               outerRef = getBNodeScopeRef(entry, columnManager);
            }
            else {
               continue;
            }
            const auto* column = &outerRef.getColumn();
            if (!needsBindings(column)) continue;

            tuples::ColumnRefAttr newRef;
            if (existingBindings) {
               if (auto existingDef = mlir::dyn_cast_or_null<tuples::ColumnDefAttr>(existingBindings.get(position))) {
                  newRef = columnManager.createRef(existingDef.getColumnPtr().get());
               }
            }
            if (!newRef) {
               auto fromExisting = mlir::ArrayAttr::get(ctxt, {outerRef});
               auto newDef = columnManager.createDef(columnManager.getUniqueScope("bindings"), position, fromExisting);
               newDef.getColumn().type = column->type;
               newRef = columnManager.createRef(newDef.getColumnPtr().get());
               bindings.emplace_back(mlir::StringAttr::get(ctxt, position), newDef);
               llvm::SmallPtrSet<mlir::Operation*, 16> visitedStale;
               fixStaleTripleColumnReferences(right, triple.getRes(), column, newRef, visitedStale);
            }
            result.push_back({outerRef, newRef});
         }
         if (!bindings.empty()) {
            triple->setAttr("bindings", mlir::DictionaryAttr::get(ctxt, bindings));
         }
      }
      return result;
   }
   void addSharedVariablePredicate(PredicateOperator join, mlir::Value left, mlir::Value right, mlir::Location loc) {
      auto* ctxt = join.getOperation()->getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedLeft;
      relalg::ColumnSet leftVars = collectTripleVariables(left, visitedLeft);
      auto pairs = ensureBindingsColumns(right, loc, [&](const tuples::Column* c) { return leftVars.contains(c); });
      if (pairs.empty()) return;

      ColumnMapper unionRemaps;
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         collectOuterJoinRemaps(right, visited, unionRemaps);
      }
      auto resolveThroughUnion = [&](tuples::ColumnRefAttr ref) {
         const tuples::Column* col = &ref.getColumn();
         for (auto it = unionRemaps.find(col); it != unionRemaps.end(); it = unionRemaps.find(col)) {
            col = it->second;
         }
         return col == &ref.getColumn() ? ref : columnManager.createRef(col);
      };

      llvm::SmallVector<mlir::Attribute> leftHash, rightHash;
      bool anyThroughUnion = false;
      for (auto& pair : pairs) {
         auto bindingsRef = resolveThroughUnion(pair.bindingsRef);
         if (&bindingsRef.getColumn() != &pair.bindingsRef.getColumn()) anyThroughUnion = true;
         join.addPredicate([&](mlir::Value tuple, mlir::OpBuilder& builder) -> mlir::Value {
            mlir::Value lhsVal = builder.create<tuples::GetColumnOp>(loc, pair.outerRef.getColumn().type, pair.outerRef, tuple);
            mlir::Value rhsVal = builder.create<tuples::GetColumnOp>(loc, bindingsRef.getColumn().type, bindingsRef, tuple);
            return builder.create<gpm::IdentifiersEqualOp>(loc, builder.getI1Type(), lhsVal, rhsVal);
         });
         leftHash.push_back(pair.outerRef);
         rightHash.push_back(bindingsRef);
      }
      if (anyThroughUnion) return;
      join->setAttr("leftHash", mlir::ArrayAttr::get(ctxt, leftHash));
      join->setAttr("rightHash", mlir::ArrayAttr::get(ctxt, rightHash));
      join->setAttr("impl", mlir::StringAttr::get(ctxt, "hash"));
      join->setAttr("useHashJoin", mlir::UnitAttr::get(ctxt));
      llvm::SmallVector<mlir::Attribute> nullsEqual(leftHash.size(), mlir::IntegerAttr::get(mlir::IntegerType::get(ctxt, 8), 0));
      join->setAttr("nullsEqual", mlir::ArrayAttr::get(ctxt, nullsEqual));
      llvm::SmallVector<mlir::Attribute> nullMatchesAll(leftHash.size(), mlir::IntegerAttr::get(mlir::IntegerType::get(ctxt, 8), 1));
      // Special outer join semantics for null (unbound) handling
      join->setAttr("nullMatchesAll", mlir::ArrayAttr::get(ctxt, nullMatchesAll));
   }
   void fixStaleTripleColumnReferences(mlir::Value subtreeRoot, mlir::Value tripleResult, const tuples::Column* oldColumn, tuples::ColumnRefAttr newRef, llvm::SmallPtrSetImpl<mlir::Operation*>& visited) {
      auto* op = subtreeRoot.getDefiningOp();
      if (!op || !visited.insert(op).second) return;
      if (llvm::is_contained(op->getOperands(), tripleResult)) {
         for (llvm::StringRef attrName : {"leftHash", "rightHash"}) {
            if (auto arr = op->getAttrOfType<mlir::ArrayAttr>(attrName)) {
               bool changed = false;
               llvm::SmallVector<mlir::Attribute> newElems;
               for (auto e : arr) {
                  if (auto ref = mlir::dyn_cast<tuples::ColumnRefAttr>(e); ref && &ref.getColumn() == oldColumn) {
                     newElems.push_back(newRef);
                     changed = true;
                  } else {
                     newElems.push_back(e);
                  }
               }
               if (changed) op->setAttr(attrName, mlir::ArrayAttr::get(op->getContext(), newElems));
            }
         }
         op->walk([&](tuples::GetColumnOp getColumnOp) {
            if (&getColumnOp.getAttr().getColumn() == oldColumn) {
               getColumnOp.setAttrAttr(newRef);
            }
         });
      }
      for (auto operand : op->getOperands()) {
         fixStaleTripleColumnReferences(operand, tripleResult, oldColumn, newRef, visited);
      }
   }
   mlir::ArrayAttr buildNullableMapping(mlir::OpBuilder& builder, const relalg::ColumnSet& createdVars, ColumnMapper& colMap) {
      auto* ctxt = builder.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto scope = columnManager.getUniqueScope("outerjoin");
      llvm::SmallVector<mlir::Attribute> mappingEntries;
      for (const auto* column : createdVars) {
         auto [origScope, name] = columnManager.getName(column);
         auto fromExisting = mlir::ArrayAttr::get(ctxt, {columnManager.createRef(column)});
         auto newDef = columnManager.createDef(scope, name, fromExisting);
         mlir::Type newType = column->type;
         if (!mlir::isa<db::NullableType>(newType)) {
            newType = db::NullableType::get(ctxt, newType);
         }
         newDef.getColumn().type = newType;
         mappingEntries.push_back(newDef);
         colMap[column] = &newDef.getColumn();
      }
      return mlir::ArrayAttr::get(ctxt, mappingEntries);
   }
   mlir::Attribute remapColumnAttr(mlir::Attribute attr, const llvm::DenseMap<const tuples::Column*, const tuples::Column*>& colMap, tuples::ColumnManager& columnManager) {
      if (!attr) return attr;
      if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(attr)) {
         auto binding = varTerm.getBinding();
         auto newBinding = remapColumnAttr(binding, colMap, columnManager);
         if (newBinding == binding) return attr;
         return gpm::VariableTermAttr::get(attr.getContext(), newBinding);
      }
      if (auto colRef = mlir::dyn_cast<tuples::ColumnRefAttr>(attr)) {
         auto it = colMap.find(&colRef.getColumn());
         if (it != colMap.end()) return columnManager.createRef(it->second);
         return attr;
      }
      if (auto colDef = mlir::dyn_cast<tuples::ColumnDefAttr>(attr)) {
         auto fromExisting = colDef.getFromExisting();
         if (!fromExisting) return attr;
         auto newFromExisting = remapColumnAttr(fromExisting, colMap, columnManager);
         if (newFromExisting == fromExisting) return attr;
         return columnManager.createDef(&colDef.getColumn(), newFromExisting);
      }
      if (auto sortSpec = mlir::dyn_cast<relalg::SortSpecificationAttr>(attr)) {
         auto newRef = remapColumnAttr(sortSpec.getAttr(), colMap, columnManager);
         if (newRef == sortSpec.getAttr()) return attr;
         return relalg::SortSpecificationAttr::get(attr.getContext(), mlir::cast<tuples::ColumnRefAttr>(newRef), sortSpec.getSortSpec());
      }
      if (auto arr = mlir::dyn_cast<mlir::ArrayAttr>(attr)) {
         bool changed = false;
         llvm::SmallVector<mlir::Attribute> newElems;
         newElems.reserve(arr.size());
         for (auto e : arr) {
            auto ne = remapColumnAttr(e, colMap, columnManager);
            changed |= ne != e;
            newElems.push_back(ne);
         }
         return changed ? mlir::ArrayAttr::get(arr.getContext(), newElems) : attr;
      }
      if (auto dict = mlir::dyn_cast<mlir::DictionaryAttr>(attr)) {
         bool changed = false;
         llvm::SmallVector<mlir::NamedAttribute> newElems;
         for (auto na : dict) {
            auto ne = remapColumnAttr(na.getValue(), colMap, columnManager);
            changed |= ne != na.getValue();
            newElems.emplace_back(na.getName(), ne);
         }
         return changed ? mlir::DictionaryAttr::get(dict.getContext(), newElems) : attr;
      }
      return attr;
   }
   void collectSubtreeOps(mlir::Value v, llvm::SmallPtrSetImpl<mlir::Operation*>& ops) {
      auto* op = v.getDefiningOp();
      if (!op || ops.contains(op)) return;
      op->walk([&](mlir::Operation* nested) { ops.insert(nested); });
      for (auto operand : op->getOperands()) collectSubtreeOps(operand, ops);
   }
   void remapColumnsEverywhere(mlir::Operation* skip, const ColumnMapper& colMap, const llvm::SmallPtrSetImpl<mlir::Operation*>* exclude = nullptr) {
      if (colMap.empty()) return;
      auto& columnManager = skip->getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      getOperation()->walk([&](mlir::Operation* op) {
         if (op == skip) return;
         if (exclude && exclude->contains(op)) return;
         bool changed = false;
         llvm::SmallVector<mlir::NamedAttribute> newAttrs;
         for (auto namedAttr : op->getAttrDictionary()) {
            auto remapped = remapColumnAttr(namedAttr.getValue(), colMap, columnManager);
            changed |= remapped != namedAttr.getValue();
            newAttrs.emplace_back(namedAttr.getName(), remapped);
         }
         if (changed) op->setAttrs(mlir::DictionaryAttr::get(op->getContext(), newAttrs));
      });
   }
   void annotateBNodeScope(gpm::TriplePatternOp triple, llvm::StringMap<tuples::ColumnRefAttr>& scope) {
      auto* ctxt = triple.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::StringMap<mlir::Attribute> usedHere;
      for (mlir::Attribute term : {triple.getS(), triple.getP(), triple.getO()}) {
         auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term);
         if (!bnode) continue;
         auto localId = bnode.getLocalId().getValue();
         auto it = scope.find(localId);
         mlir::Attribute entry;
         if (it == scope.end()) {
            auto uniqueScope = columnManager.getUniqueScope("bnode");
            auto def = columnManager.createDef(uniqueScope, localId.str());
            def.getColumn().type = gpm::VariableBindingType::get(ctxt);
            scope[localId] = columnManager.createRef(def.getColumnPtr().get());
            entry = def;
         }
         else {
            entry = it->second;
         }
         usedHere[localId] = entry;
      }
      if (!usedHere.empty()) {
         llvm::SmallVector<mlir::NamedAttribute> entries;
         for (auto& entry : usedHere)
            entries.emplace_back(mlir::StringAttr::get(ctxt, entry.getKey()), entry.getValue());
         triple->setAttr("bnodeScope", mlir::DictionaryAttr::get(ctxt, entries));
      }
   }
};

} // namespace

std::unique_ptr<mlir::Pass> gpm::createUnnestGraphPatternsPass() { return std::make_unique<UnnestGraphPatternsPass>(); }
