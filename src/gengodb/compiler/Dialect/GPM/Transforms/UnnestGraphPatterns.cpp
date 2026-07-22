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

class UnnestGraphPatternsPass : public mlir::PassWrapper<UnnestGraphPatternsPass, mlir::OperationPass<mlir::ModuleOp>> {
   virtual llvm::StringRef getArgument() const override { return "gpm-unnest-patterns"; }

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnnestGraphPatternsPass)

   void runOnOperation() override {
      llvm::SmallVector<GraphPatternOp> roots;
      getOperation()->walk([&](mlir::Operation* op) {
         auto patternOp = mlir::dyn_cast<GraphPatternOp>(op);
         if (!patternOp) return;
         if (mlir::isa_and_nonnull<GraphPatternOp>(op->getParentOp())) return;
         roots.push_back(patternOp);
      });

      llvm::DenseMap<mlir::Operation*, unsigned> indexOf;
      for (auto [i, root] : llvm::enumerate(roots)) indexOf[root.getOperation()] = i;
      llvm::SmallVector<unsigned> parent(roots.size());
      for (unsigned i = 0; i < roots.size(); ++i) parent[i] = i;
      auto find = [&](unsigned x) {
         while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
         }
         return x;
      };
      llvm::SmallPtrSet<mlir::Operation*, 16> rootSet;
      for (auto root : roots) rootSet.insert(root.getOperation());
      for (auto [i, root] : llvm::enumerate(roots)) {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         if (auto* ancestor = findAncestorRoot(root.getRel(), visited, rootSet)) {
            unsigned a = find(i), b = find(indexOf[ancestor]);
            if (a != b) parent[a] = b;
         }
      }

      llvm::MapVector<unsigned, llvm::SmallVector<GraphPatternOp>> components;
      for (auto [i, root] : llvm::enumerate(roots)) components[find(i)].push_back(root);
      for (auto& [key, component] : components) {
         mlir::Value stream;
         bool first = true;
         for (auto root : component) {
            processElement(root, root.getOperation(), stream, first);
         }
      }
   }

   private:
   mlir::Operation* findAncestorRoot(mlir::Value v, llvm::SmallPtrSetImpl<mlir::Operation*>& visited, const llvm::SmallPtrSetImpl<mlir::Operation*>& rootSet) {
      auto* op = v.getDefiningOp();
      if (!op || !visited.insert(op).second) return nullptr;
      if (rootSet.contains(op)) return op;
      for (auto operand : op->getOperands()) {
         if (auto* found = findAncestorRoot(operand, visited, rootSet)) return found;
      }
      return nullptr;
   }
   void processElement(GraphPatternOp patternOp, mlir::Operation* insertBefore, mlir::Value& stream, bool& first) {
      auto createdVars = mlir::cast<GPMOperator>(patternOp.getOperation()).getCreatedVariables();
      auto kind = patternOp.getPatternKind();
      auto loc = patternOp.getLoc();
      mlir::Operation* rawOp = patternOp.getOperation();
      mlir::Value rel = patternOp.getRel();
      mlir::Value elementStream = unnestInto(patternOp, insertBefore);
      foldIntoAccumulator(stream, first, elementStream, kind, createdVars, insertBefore, loc);
      rawOp->getResult(0).replaceAllUsesWith(stream);
      rawOp->erase();
      if (auto* relDefOp = rel.getDefiningOp()) {
         if (relDefOp->use_empty()) relDefOp->erase();
      }
   }
   mlir::Value unnestInto(GraphPatternOp patternOp, mlir::Operation* insertBefore) {
      llvm::StringMap<tuples::ColumnRefAttr> bnodeScope;
      mlir::Value stream;
      bool first = true;
      for (auto& op : llvm::make_early_inc_range(patternOp.getPattern().front().without_terminator())) {
         if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(&op)) {
            annotateBNodeScope(triple, bnodeScope);
            isolateTriple(triple, insertBefore);
            mlir::Value elementStream = triple.getRes();
            foldIntoAccumulator(stream, first, elementStream, gpm::PatternKind::basic, relalg::ColumnSet(), insertBefore, triple.getLoc());
         } 
         else if (auto nested = mlir::dyn_cast<GraphPatternOp>(&op)) {
            processElement(nested, insertBefore, stream, first);
         } 
         else {
            op.emitOpError("unnesting of this operator nested inside a graph pattern is not supported");
            signalPassFailure();
            return stream;
         }
      }
      return stream;
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
   void foldIntoAccumulator(mlir::Value& stream, bool& first, mlir::Value elementStream, gpm::PatternKind elementKind, const relalg::ColumnSet& elementCreatedVars, mlir::Operation* insertBefore, mlir::Location loc) {
      if (first) {
         stream = elementStream;
         first = false;
         return;
      }
      mlir::OpBuilder builder(insertBefore->getContext());
      builder.setInsertionPoint(insertBefore);
      auto streamType = tuples::TupleStreamType::get(builder.getContext());
      if (elementKind == gpm::PatternKind::optional) {
         auto mapping = buildNullableMapping(builder, elementCreatedVars);
         auto join = builder.create<relalg::OuterJoinOp>(loc, streamType, stream, elementStream, mapping);
         join.initPredicate();
         addSharedVariablePredicate(join, stream, elementStream, loc);
         stream = join.getResult();
      }
      else if (elementKind == gpm::PatternKind::minus) {
         auto join = builder.create<relalg::AntiSemiJoinOp>(loc, streamType, stream, elementStream);
         join.initPredicate();
         addSharedVariablePredicate(join, stream, elementStream, loc);
         stream = join.getResult();
      }
      else {
         auto join = builder.create<relalg::InnerJoinOp>(loc, streamType, stream, elementStream);
         join.initPredicate();
         addSharedVariablePredicate(join, stream, elementStream, loc);
         stream = join.getResult();
      }
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
               result.insert(&mlir::cast<tuples::ColumnRefAttr>(entry.getValue()).getColumn());
            }
         }
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
      for (auto operand : op->getOperands()) {
         collectTriples(operand, visited, result);
      }
   }
   void addSharedVariablePredicate(PredicateOperator join, mlir::Value left, mlir::Value right, mlir::Location loc) {
      auto* ctxt = join.getOperation()->getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedLeft;
      relalg::ColumnSet leftVars = collectTripleVariables(left, visitedLeft);
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedRight;
      llvm::SmallVector<gpm::TriplePatternOp> rightTriples;
      collectTriples(right, visitedRight, rightTriples);

      llvm::SmallVector<mlir::Attribute> leftHash, rightHash;
      for (auto triple : rightTriples) {
         llvm::SmallVector<mlir::NamedAttribute> bindings;
         auto bnodeScope = triple->getAttrOfType<mlir::DictionaryAttr>("bnodeScope");
         for (auto [position, term] : {std::pair{"s", triple.getS()}, std::pair{"p", triple.getP()}, std::pair{"o", triple.getO()}}) {
            tuples::ColumnRefAttr leftRef;
            if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
               if (!varTerm.hasBinding()) continue;
               leftRef = varTerm.getBindingReference();
            } 
            else if (auto bnodeTerm = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
               if (!bnodeScope) continue;
               auto entry = bnodeScope.get(bnodeTerm.getLocalId().getValue());
               if (!entry) continue;
               leftRef = mlir::cast<tuples::ColumnRefAttr>(entry);
            }
            else {
               continue;
            }
            const auto* column = &leftRef.getColumn();
            if (!leftVars.contains(column)) continue;

            auto fromExisting = mlir::ArrayAttr::get(ctxt, {leftRef});
            auto newDef = columnManager.createDef(columnManager.getUniqueScope("bindings"), position, fromExisting);
            newDef.getColumn().type = column->type;
            auto newRef = columnManager.createRef(newDef.getColumnPtr().get());

            bindings.emplace_back(mlir::StringAttr::get(ctxt, position), newDef);
            join.addPredicate([&](mlir::Value tuple, mlir::OpBuilder& builder) -> mlir::Value {
               return builder.create<gpm::IdentifiersEqualOp>(loc, builder.getI1Type(), tuple, leftRef, newRef);
            });
            leftHash.push_back(leftRef);
            rightHash.push_back(newRef);
         }
         if (!bindings.empty()) {
            triple->setAttr("bindings", mlir::DictionaryAttr::get(ctxt, bindings));
         }
      }
      if (leftHash.empty()) return;
      join->setAttr("leftHash", mlir::ArrayAttr::get(ctxt, leftHash));
      join->setAttr("rightHash", mlir::ArrayAttr::get(ctxt, rightHash));
      join->setAttr("impl", mlir::StringAttr::get(ctxt, "hash"));
      join->setAttr("useHashJoin", mlir::UnitAttr::get(ctxt));
      llvm::SmallVector<mlir::Attribute> nullsEqual(leftHash.size(), mlir::IntegerAttr::get(mlir::IntegerType::get(ctxt, 8), 0));
      join->setAttr("nullsEqual", mlir::ArrayAttr::get(ctxt, nullsEqual));
   }
   mlir::ArrayAttr buildNullableMapping(mlir::OpBuilder& builder, const relalg::ColumnSet& createdVars) {
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
      }
      return mlir::ArrayAttr::get(ctxt, mappingEntries);
   }
   void annotateBNodeScope(gpm::TriplePatternOp triple, llvm::StringMap<tuples::ColumnRefAttr>& scope) {
      auto* ctxt = triple.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::StringMap<tuples::ColumnRefAttr> usedHere;
      for (mlir::Attribute term : {triple.getS(), triple.getP(), triple.getO()}) {
         auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term);
         if (!bnode) continue;
         auto localId = bnode.getLocalId().getValue();
         auto it = scope.find(localId);
         tuples::ColumnRefAttr ref;
         if (it == scope.end()) {
            auto uniqueScope = columnManager.getUniqueScope("bnode");
            auto def = columnManager.createDef(uniqueScope, localId.str());
            def.getColumn().type = gpm::VariableBindingType::get(ctxt);
            ref = columnManager.createRef(def.getColumnPtr().get());
            scope[localId] = ref;
         } 
         else {
            ref = it->second;
         }
         usedHere[localId] = ref;
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
