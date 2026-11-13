#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/Transforms/BindingColumnTransformer.h"
#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"
#include "gengodb/compiler/Dialect/Variant/VariantOps.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"

#include <cmath>

namespace {
using namespace gengodb::compiler::dialect;
using namespace lingodb::compiler::dialect;
using ColumnMapper = llvm::DenseMap<const tuples::Column*, const tuples::Column*>;
using ColumnPairs = llvm::SmallVector<std::pair<const tuples::Column*, const tuples::Column*>>;
constexpr gpm::TripleSlot kTripleSlots[] = {gpm::TripleSlot::subject, gpm::TripleSlot::predicate, gpm::TripleSlot::object};
llvm::StringRef slotName(gpm::TripleSlot slot) {
   switch (slot) {
      case gpm::TripleSlot::subject: return "s";
      case gpm::TripleSlot::predicate: return "p";
      default: return "o";
   }
}
mlir::Attribute getSlot(gpm::TriplePatternOp triple, gpm::TripleSlot slot) {
   switch (slot) {
      case gpm::TripleSlot::subject: return triple.getS();
      case gpm::TripleSlot::predicate: return triple.getP();
      default: return triple.getO();
   }
}
void setSlot(gpm::TriplePatternOp triple, gpm::TripleSlot slot, mlir::Attribute term) {
   switch (slot) {
      case gpm::TripleSlot::subject: triple.setSAttr(term); break;
      case gpm::TripleSlot::predicate: triple.setPAttr(term); break;
      default: triple.setOAttr(term); break;
   }
}
tuples::ColumnRefAttr bindingSource(gpm::VariableTermAttr varTerm) {
   auto def = varTerm.getProducedBinding();
   if (!def) return {};
   auto fromExisting = mlir::dyn_cast_or_null<mlir::ArrayAttr>(def.getFromExisting());
   if (!fromExisting || fromExisting.empty()) return {};
   return mlir::dyn_cast<tuples::ColumnRefAttr>(fromExisting[0]);
}
const tuples::Column* resolveThroughRemaps(const ColumnMapper& remaps, const tuples::Column* column) {
   for (auto it = remaps.find(column); it != remaps.end(); it = remaps.find(column)) {
      column = it->second;
   }
   return column;
}
bool isGpmStreamOp(mlir::Operation* op) {
   return op && (mlir::isa<gpm::GraphPatternOp>(op) || mlir::isa<gpm::BagOp, gpm::TriplePatternOp, gpm::NamedGraphOp, gpm::FilterOp, gpm::ExtendOp>(op));
}
relalg::MapOp asBindMap(mlir::Operation* op) {
   auto mapOp = mlir::dyn_cast_or_null<relalg::MapOp>(op);
   return mapOp && mapOp->hasAttr(gpm::kBindAttr) ? mapOp : relalg::MapOp();
}
void insertComputedColumns(relalg::MapOp mapOp, relalg::ColumnSet& result) {
   for (auto attr : mapOp.getComputedCols()) result.insert(mlir::cast<tuples::ColumnDefAttr>(attr).getColumnPtr().get());
}
bool isNestedInGraphPattern(mlir::Operation* op) {
   for (auto* parent = op->getParentOp(); parent; parent = parent->getParentOp()) {
      if (mlir::isa<gpm::GraphPatternOp>(parent)) return true;
   }
   return false;
}
mlir::Value createUnitRelation(mlir::OpBuilder& builder, mlir::Location loc) {
   auto& columnManager = builder.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto unitDef = columnManager.createDef(columnManager.getUniqueScope("unit"), "unit");
   unitDef.getColumn().type = builder.getI32Type();
   auto row = builder.getArrayAttr({builder.getI32IntegerAttr(0)});
   return builder.create<relalg::ConstRelationOp>(loc, builder.getArrayAttr({unitDef}), builder.getArrayAttr({row}));
}

class UnnestGraphPatternsPass : public mlir::PassWrapper<UnnestGraphPatternsPass, mlir::OperationPass<mlir::ModuleOp>> {
   virtual llvm::StringRef getArgument() const override { return "gpm-unnest-patterns"; }

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnnestGraphPatternsPass)

   void getDependentDialects(mlir::DialectRegistry& registry) const override {
      registry.insert<mlir::arith::ArithDialect, variant::VariantDialect>();
   }

   void runOnOperation() override {
      resolveBindingTypes();
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
      getOperation()->walk([&](mlir::Operation* op) {
         if (auto expansion = mlir::dyn_cast<gpm::NodeExpansionOp>(op)) rescopeGraph(expansion);
         if (auto expansion = mlir::dyn_cast<gpm::EdgeExpansionOp>(op)) rescopeGraph(expansion);
      });
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
      normalizeTriples();
   }

   private:
   static mlir::Type resolvedBindingType(mlir::Type type) {
      auto* ctxt = type.getContext();
      if (mlir::isa<gpm::VariableBindingType>(type)) return variant::VariantType::get(ctxt);
      if (auto nullableType = mlir::dyn_cast<db::NullableType>(type)) {
         if (mlir::isa<gpm::VariableBindingType>(nullableType.getType())) return db::NullableType::get(ctxt, variant::VariantType::get(ctxt));
      }
      return type;
   }
   static void resolveColumnType(tuples::Column& column) {
      if (column.type) column.type = resolvedBindingType(column.type);
   }
   void resolveBindingTypes() {
      getOperation()->walk([&](mlir::Operation* op) {
         op->getAttrDictionary().walk([&](mlir::Attribute attr) {
            if (auto def = mlir::dyn_cast<tuples::ColumnDefAttr>(attr)) resolveColumnType(def.getColumn());
            else if (auto ref = mlir::dyn_cast<tuples::ColumnRefAttr>(attr)) resolveColumnType(ref.getColumn());
         });
      });
      getOperation()->walk([&](tuples::GetColumnOp getColumnOp) { syncResultType(getColumnOp); });
   }
   static mlir::Type producedBindingType(mlir::Type type) {
      if (auto nullableType = mlir::dyn_cast<db::NullableType>(type)) return nullableType.getType();
      return type;
   }
   static mlir::Type unionMappingType(mlir::ArrayAttr fromExisting) {
      bool nullable = false;
      mlir::Type mergedType;
      for (mlir::Attribute side : fromExisting) {
         auto sourceRef = mlir::dyn_cast<tuples::ColumnRefAttr>(side);
         if (!sourceRef) {
            nullable = true;
            continue;
         }
         mlir::Type sideType = sourceRef.getColumn().type;
         if (auto nullableType = mlir::dyn_cast<db::NullableType>(sideType)) {
            nullable = true;
            sideType = nullableType.getType();
         }
         if (!mergedType) mergedType = sideType;
      }
      if (!mergedType) return {};
      return nullable ? static_cast<mlir::Type>(db::NullableType::get(fromExisting.getContext(), mergedType)) : mergedType;
   }
   static void syncResultType(tuples::GetColumnOp getColumnOp) {
      mlir::Type liveType = getColumnOp.getAttr().getColumn().type;
      if (getColumnOp.getResult().getType() != liveType) getColumnOp.getResult().setType(liveType);
   }

   llvm::DenseMap<mlir::Value, mlir::Value> rewritten;
   mlir::Operation* insertPoint = nullptr;
   llvm::StringMap<tuples::ColumnRefAttr>* bnodeScope = nullptr;
   llvm::MapVector<const tuples::Column*, const tuples::Column*> splitOrigin;
   ColumnMapper nameMerges;
   llvm::DenseMap<const tuples::Column*, tuples::ColumnRefAttr> isolatedGraphs;
   struct FuzzyScope {
      std::optional<float> threshold;
      llvm::DenseMap<mlir::Value, const tuples::Column*> degreeOf;
      llvm::DenseSet<mlir::Value> pruned;
   };
   llvm::SmallVector<FuzzyScope> fuzzyScopes;
   template <typename OpTy>
   void rescopeGraph(OpTy expansion) {
      if (auto it = isolatedGraphs.find(&expansion.getGraphRef().getColumn()); it != isolatedGraphs.end()) expansion.setGraphRefAttr(it->second);
   }
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
      if (auto patternOp = mlir::dyn_cast<gpm::GraphPatternOp>(op)) {
         result = rewritePattern(patternOp);
      } 
      else if (auto unionOp = mlir::dyn_cast<gpm::BagOp>(op)) {
         result = rewriteUnion(unionOp);
      }
      else if (auto filterOp = mlir::dyn_cast<gpm::FilterOp>(op)) {
         result = rewriteFilter(filterOp);
      }
      else if (auto extendOp = mlir::dyn_cast<gpm::ExtendOp>(op)) {
         result = rewriteExtend(extendOp);
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
      bnodesToVariables(triple, bnodeScope ? *bnodeScope : ownScope);
      isolateTriple(triple, insertPoint);
      if (!fuzzyScopes.empty()) attachDegree(triple);
      return fold(accumulator, triple.getRes(), gpm::PatternKind::basic, relalg::ColumnSet(), triple.getLoc());
   }
   void attachDegree(gpm::TriplePatternOp triple) {
      auto& columnManager = triple.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto degreeDef = columnManager.createDef(columnManager.getUniqueScope("fuzzy"), "degree");
      degreeDef.getColumn().type = mlir::Float32Type::get(triple.getContext());
      triple.setTruthDegrAttr(degreeDef);
      for (auto& scope : fuzzyScopes) scope.degreeOf[triple.getRes()] = &degreeDef.getColumn();
   }
   void propagateDegrees(mlir::Value from, mlir::Value to) {
      for (auto& scope : fuzzyScopes) {
         if (const auto* degree = scope.degreeOf.lookup(from)) scope.degreeOf[to] = degree;
      }
   }
   mlir::Value rewriteFilter(gpm::FilterOp filterOp) {
      mlir::Value newRel = rewrite(filterOp.getRel());
      mlir::Value rel = newRel ? newRel : filterOp.getRel();
      mlir::OpBuilder builder(filterOp.getContext());
      builder.setInsertionPoint(insertPoint);
      auto selOp = builder.create<relalg::SelectionOp>(filterOp.getLoc(), filterOp.getRes().getType(), rel);
      selOp.getPredicate().takeBody(filterOp.getPredicate());
      propagateDegrees(rel, selOp.getResult());
      return selOp.getResult();
   }
   mlir::Value rewriteExtend(gpm::ExtendOp extendOp) {
      auto* ctxt = extendOp.getContext();
      auto loc = extendOp.getLoc();
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      mlir::Value rel = rewrite(extendOp.getRel());
      if (!rel) rel = createUnitRelation(builder, loc); // seed input (named graph / pattern region entry)
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), rel, builder.getArrayAttr({extendOp.getComputed()}));
      mapOp.getPredicate().takeBody(extendOp.getExpression());
      mapOp->setAttr(gpm::kBindAttr, builder.getUnitAttr());
      propagateDegrees(rel, mapOp.getResult());
      return mapOp.getResult();
   }
   mlir::Value rewriteFuzzy(gpm::FuzzyGraphPatternOp fuzzyOp) {
      auto loc = fuzzyOp.getLoc();
      mlir::Value accumulator = rewrite(fuzzyOp.getRel());
      mlir::Value blockArg = fuzzyOp.getPattern().front().getArgument(0);
      rewritten[blockArg] = accumulator;
      std::optional<gpm::Hedge> hedge = fuzzyOp.getHedge();
      std::optional<DegreeBound> bound;
      if (auto attr = fuzzyOp.getThreshold()) bound = boundOnRawDegree(attr->convertToFloat(), hedge);
      fuzzyScopes.emplace_back().threshold = bound && !bound->upper ? std::optional<float>(bound->value) : std::nullopt;
      mlir::Value result = rewriteRegion(fuzzyOp.getPattern());
      FuzzyScope scope = fuzzyScopes.pop_back_val();
      rewritten.erase(blockArg);
      if (!result || result == accumulator) return result;
      const auto* degree = scope.degreeOf.lookup(result);
      if (!degree) {
         fuzzyOp.emitOpError("TRUTH OF block matches no triples");
         signalPassFailure();
         return {};
      }
      mlir::Value finalStream;
      if (!bound) {
         mlir::Value degreeStream = result;
         if (hedge) std::tie(degreeStream, degree) = buildHedge(result, degree, *hedge, loc);
         finalStream = bindTruthDegree(degreeStream, degree, fuzzyOp.getTruthDegrAttr(), loc);
      }
      else if (!bound->upper && scope.pruned.contains(result)) {
         finalStream = result;
      }
      else {
         auto predicate = bound->upper ? mlir::arith::CmpFPredicate::OLE : mlir::arith::CmpFPredicate::OGE;
         finalStream = buildThresholdSelection(result, degree, bound->value, /*keepUnbound=*/false, loc, predicate);
      }
      propagateDegrees(result, finalStream);
      return finalStream;
   }
   struct DegreeBound {
      float value;
      bool upper;
   };
   static DegreeBound boundOnRawDegree(float threshold, std::optional<gpm::Hedge> hedge) {
      double t = threshold;
      if (!hedge) return {threshold, false};
      switch (*hedge) {
         case gpm::Hedge::very: return {static_cast<float>(std::sqrt(t)), false};
         case gpm::Hedge::extremely: return {static_cast<float>(std::cbrt(t)), false};
         case gpm::Hedge::more_or_less: return {static_cast<float>(t * t), false};
         case gpm::Hedge::negation: return {static_cast<float>(1.0 - t), true};
      }
      llvm_unreachable("unknown fuzzy hedge");
   }
   std::pair<mlir::Value, const tuples::Column*> buildHedge(mlir::Value stream, const tuples::Column* degree, gpm::Hedge hedge, mlir::Location loc) {
      auto* ctxt = stream.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      mlir::Type f32 = mlir::Float32Type::get(ctxt);
      mlir::Type f64 = mlir::Float64Type::get(ctxt);
      bool nullable = mlir::isa<db::NullableType>(degree->type);
      auto hedgedDef = columnManager.createDef(columnManager.getUniqueScope("fuzzy"), "hedged");
      hedgedDef.getColumn().type = degree->type;
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      auto* block = new mlir::Block;
      mlir::Value tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         mlir::Value value = builder.create<tuples::GetColumnOp>(loc, degree->type, columnManager.createRef(degree), tuple);
         mlir::Value isNull;
         mlir::Value x = value;
         if (nullable) {
            isNull = builder.create<db::IsNullOp>(loc, value);
            x = builder.create<db::NullableGetVal>(loc, value);
         }
         mlir::Value hedged;
         switch (hedge) {
            case gpm::Hedge::very:
               hedged = builder.create<mlir::arith::MulFOp>(loc, x, x);
               break;
            case gpm::Hedge::extremely:
               hedged = builder.create<mlir::arith::MulFOp>(loc, builder.create<mlir::arith::MulFOp>(loc, x, x), x);
               break;
            case gpm::Hedge::more_or_less: {
               mlir::Value wide = builder.create<mlir::arith::ExtFOp>(loc, f64, x);
               mlir::Value root = builder.create<db::RuntimeCall>(loc, f64, "Sqrt", mlir::ValueRange({wide})).getRes();
               hedged = builder.create<mlir::arith::TruncFOp>(loc, f32, root);
               break;
            }
            case gpm::Hedge::negation: {
               mlir::Value one = builder.create<mlir::arith::ConstantOp>(loc, f32, builder.getF32FloatAttr(1.0f));
               hedged = builder.create<mlir::arith::SubFOp>(loc, one, x);
               break;
            }
         }
         if (nullable) hedged = builder.create<db::AsNullableOp>(loc, degree->type, hedged, isNull);
         builder.create<tuples::ReturnOp>(loc, hedged);
      }
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, builder.getArrayAttr({hedgedDef}));
      mapOp.getPredicate().push_back(block);
      return {mapOp.getResult(), &hedgedDef.getColumn()};
   }
   mlir::Value rewritePattern(gpm::GraphPatternOp patternOp) {
      if (auto fuzzyOp = mlir::dyn_cast<gpm::FuzzyGraphPatternOp>(patternOp.getOperation())) return rewriteFuzzy(fuzzyOp);
      auto createdVars = mlir::cast<gpm::GPMOperator>(patternOp.getOperation()).getCreatedVariables();
      auto kind = patternOp.getPatternKind();
      auto loc = patternOp.getLoc();
      mlir::Value accumulator = rewrite(patternOp.getRel());
      mlir::Value elementStream = rewriteRegion(patternOp.getPattern());
      if (accumulator && elementStream && (kind == gpm::PatternKind::basic || kind == gpm::PatternKind::optional)) {
         if (!splitDanglingReferences(patternOp, accumulator, elementStream, createdVars)) return {};
      }
      ColumnMapper nestedRemaps;
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedRemaps;
      collectOuterJoinRemaps(elementStream, visitedRemaps, nestedRemaps);
      relalg::ColumnSet liveCreatedVars;
      for (const auto* column : createdVars) {
         liveCreatedVars.insert(resolveThroughRemaps(nestedRemaps, column));
      }
      ColumnPairs splitPairs;
      if (accumulator && elementStream) splitPairs = matchSplitVariables(accumulator, elementStream, nestedRemaps);
      if (!splitPairs.empty() && kind == gpm::PatternKind::minus) {
         auto& columnManager = patternOp->getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
         patternOp->emitOpError("unsupported: variable ?") << columnManager.getName(splitPairs.front().first).second << " is bound independently inside a MINUS and outside of it";
         signalPassFailure();
         return {};
      }
      return fold(accumulator, elementStream, kind, liveCreatedVars, loc, splitPairs);
   }
   bool splitDanglingReferences(gpm::GraphPatternOp patternOp, mlir::Value accumulator, mlir::Value elementStream, relalg::ColumnSet& createdVars) {
      auto* ctxt = patternOp->getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      relalg::ColumnSet leftVars;
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         leftVars = collectTripleVariables(accumulator, visited);
      }
      relalg::ColumnSet elementProduced = producedColumns(elementStream);
      llvm::SmallVector<gpm::TriplePatternOp> triples;
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         collectTriples(elementStream, visited, triples);
      }
      ColumnMapper splits;
      for (auto triple : triples) {
         for (auto slot : kTripleSlots) {
            auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(getSlot(triple, slot));
            if (!varTerm || !varTerm.hasBinding()) continue;
            const auto* column = &varTerm.getBindingReference().getColumn();
            if (leftVars.contains(column) || elementProduced.contains(column)) continue;
            if (splits.contains(column)) {
               triple.emitOpError("unsupported: variable ?") << columnManager.getName(column).second << " is bound more than once inside an independently evaluated pattern";
               signalPassFailure();
               return false;
            }
            auto freshDef = columnManager.createDef(columnManager.getUniqueScope("vars"), columnManager.getName(column).second);
            freshDef.getColumn().type = producedBindingType(column->type);
            setSlot(triple, slot, gpm::VariableTermAttr::get(ctxt, freshDef));
            splits[column] = &freshDef.getColumn();
            splitOrigin[&freshDef.getColumn()] = column;
            createdVars.insert(&freshDef.getColumn());
         }
      }
      if (!splits.empty()) {
         llvm::SmallPtrSet<mlir::Operation*, 32> elementOps;
         collectSubtreeOps(elementStream, elementOps);
         gpm::BindingColumnTransformer(ctxt).remapColumnsIn(elementOps, splits);
      }
      return true;
   }
   ColumnPairs matchSplitVariables(mlir::Value accumulator, mlir::Value elementStream, const ColumnMapper& elementRemaps) {
      ColumnMapper outerRemaps;
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         collectOuterJoinRemaps(accumulator, visited, outerRemaps);
      }
      relalg::ColumnSet outerProduced = producedColumns(accumulator);
      relalg::ColumnSet innerProduced = producedColumns(elementStream);
      ColumnPairs pairs;
      for (auto [fresh, origin] : splitOrigin) {
         const auto* innerLive = resolveLive(elementRemaps, fresh);
         if (!innerProduced.contains(innerLive)) continue;
         const auto* outerLive = resolveLive(outerRemaps, origin);
         if (!outerProduced.contains(outerLive) || outerLive == innerLive) continue;
         pairs.emplace_back(outerLive, innerLive);
      }
      return pairs;
   }
   const tuples::Column* resolveLive(const ColumnMapper& remaps, const tuples::Column* column) {
      for (const tuples::Column* prev = nullptr; prev != column;) {
         prev = column;
         column = resolveThroughRemaps(nameMerges, resolveThroughRemaps(remaps, column));
      }
      return column;
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
         if (auto merged = outerJoin->getAttrOfType<mlir::ArrayAttr>(gpm::kMergedColumnsAttr)) insertMapping(merged);
         result.insert(producedColumns(outerJoin.getLeft(), visited));
         return result;
      }
      if (auto antiSemiJoin = mlir::dyn_cast<relalg::AntiSemiJoinOp>(op)) {
         result.insert(producedColumns(antiSemiJoin.getLeft(), visited));
         return result;
      }
      if (auto bindMap = asBindMap(op)) insertComputedColumns(bindMap, result);
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

      llvm::SmallVector<std::pair<FuzzyScope*, const tuples::Column*>> unionDegrees;
      for (auto& scope : fuzzyScopes) {
         const auto* leftDegree = scope.degreeOf.lookup(leftStream);
         const auto* rightDegree = scope.degreeOf.lookup(rightStream);
         if (!leftDegree && !rightDegree) continue;
         auto side = [&](const tuples::Column* degree) -> mlir::Attribute {
            return degree ? static_cast<mlir::Attribute>(columnManager.createRef(degree)) : static_cast<mlir::Attribute>(mlir::UnitAttr::get(ctxt));
         };
         auto degreeDef = columnManager.createDef(columnManager.getUniqueScope("fuzzy"), "acc", mlir::ArrayAttr::get(ctxt, {side(leftDegree), side(rightDegree)}));
         bool nullable = !leftDegree || !rightDegree || mlir::isa<db::NullableType>(leftDegree->type) || mlir::isa<db::NullableType>(rightDegree->type);
         mlir::Type f32 = mlir::Float32Type::get(ctxt);
         degreeDef.getColumn().type = nullable ? static_cast<mlir::Type>(db::NullableType::get(ctxt, f32)) : f32;
         mappingEntries.push_back(degreeDef);
         unionDegrees.emplace_back(&scope, &degreeDef.getColumn());
      }

      for (auto attr : mappingEntries) {
         auto def = mlir::cast<tuples::ColumnDefAttr>(attr);
         if (auto type = unionMappingType(mlir::cast<mlir::ArrayAttr>(def.getFromExisting()))) def.getColumn().type = type;
      }
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      auto relUnion = builder.create<relalg::UnionOp>(loc, relalg::SetSemanticAttr::get(ctxt, relalg::SetSemantic::all), leftStream, rightStream, mlir::ArrayAttr::get(ctxt, mappingEntries));
      llvm::SmallPtrSet<mlir::Operation*, 32> preMergeOps;
      collectSubtreeOps(leftStream, preMergeOps);
      collectSubtreeOps(rightStream, preMergeOps);
      gpm::BindingColumnTransformer(ctxt).remapColumnsEverywhere(getOperation(), relUnion.getOperation(), passthroughColMap, &preMergeOps);
      for (auto [scope, degree] : unionDegrees) scope->degreeOf[relUnion.getResult()] = degree;
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
      auto freshRef = columnManager.createRef(freshDef.getColumnPtr().get());
      isolatedGraphs[&triple.getGraphRef().getColumn()] = freshRef;
      triple.setGraphRefAttr(freshRef);
      triple.getRelMutable().set(namedGraph.getRes());
      triple->moveBefore(insertBefore);
   }
   mlir::Value fold(mlir::Value accumulator, mlir::Value elementStream, gpm::PatternKind elementKind, const relalg::ColumnSet& elementCreatedVars, mlir::Location loc, const ColumnPairs& splitPairs = {}) {
      if (!accumulator) return elementStream;
      mlir::OpBuilder builder(insertPoint->getContext());
      builder.setInsertionPoint(insertPoint);
      auto streamType = tuples::TupleStreamType::get(builder.getContext());
      if (elementKind == gpm::PatternKind::optional) {
         ColumnPairs sharedPairs;
         {
            llvm::SmallPtrSet<mlir::Operation*, 16> visitedLeft;
            sharedPairs = sharedVariablePairs(elementStream, loc, collectTripleVariables(accumulator, visitedLeft));
         }
         ColumnPairs mergePairs;
         for (auto pair : sharedPairs) {
            if (mlir::isa<db::NullableType>(pair.first->type)) mergePairs.push_back(pair);
         }
         for (auto pair : splitPairs) {
            if (mlir::isa<db::NullableType>(pair.first->type)) mergePairs.push_back(pair);
         }
         relalg::ColumnSet paddedVars = elementCreatedVars;
         for (auto [leftCol, rightCol] : mergePairs) paddedVars.insert(rightCol);
         for (auto& scope : fuzzyScopes) {
            if (const auto* degree = scope.degreeOf.lookup(elementStream)) paddedVars.insert(degree);
         }
         ColumnMapper nullableColMap;
         auto mapping = buildNullableMapping(builder, paddedVars, nullableColMap);
         auto join = builder.create<relalg::OuterJoinOp>(loc, streamType, accumulator, elementStream, mapping);
         join.initPredicate();
         addSharedVariablePredicate(join, sharedPairs, loc);
         addNameMatchedPredicate(join, splitPairs, loc, /*recordMerges=*/false);
         llvm::SmallPtrSet<mlir::Operation*, 32> preMergeOps;
         collectSubtreeOps(accumulator, preMergeOps);
         collectSubtreeOps(elementStream, preMergeOps);
         join->walk([&](mlir::Operation* op) { preMergeOps.insert(op); });
         for (auto [leftCol, rightCol] : splitPairs) {
            if (!mlir::isa<db::NullableType>(leftCol->type)) nameMerges[nullableColMap.lookup(rightCol)] = leftCol;
         }
         ColumnMapper colMap = nullableColMap;
         if (!mergePairs.empty()) {
            auto mergedCols = buildMergedColumns(mergePairs, nullableColMap);
            join->setAttr(gpm::kMergedColumnsAttr, mergedCols);
            for (auto [attr, pair] : llvm::zip(mergedCols, mergePairs)) {
               const auto* merged = &mlir::cast<tuples::ColumnDefAttr>(attr).getColumn();
               const auto* paddedRight = nullableColMap.lookup(pair.second);
               colMap[pair.first] = merged;
               colMap[pair.second] = merged;
               colMap[paddedRight] = merged;
               nameMerges[pair.first] = merged;
               nameMerges[paddedRight] = merged;
            }
         }
         gpm::BindingColumnTransformer(&getContext()).remapColumnsEverywhere(getOperation(), join.getOperation(), colMap, &preMergeOps);
         return combineDegrees(join.getResult(), accumulator, elementStream, nullableColMap, loc);
      }
      if (elementKind == gpm::PatternKind::minus) {
         auto join = builder.create<relalg::AntiSemiJoinOp>(loc, streamType, accumulator, elementStream);
         join.initPredicate();
         llvm::SmallPtrSet<mlir::Operation*, 16> visitedLeft;
         auto pairs = sharedVariablePairs(elementStream, loc, collectTripleVariables(accumulator, visitedLeft));
         addSharedVariablePredicate(join, pairs, loc);
         addDomainOverlapPredicate(join, pairs, loc);
         return combineDegrees(join.getResult(), accumulator, mlir::Value(), ColumnMapper(), loc);
      }
      auto join = builder.create<relalg::InnerJoinOp>(loc, streamType, accumulator, elementStream);
      join.initPredicate();
      addSharedVariablePredicate(join, accumulator, elementStream, loc);
      addNameMatchedPredicate(join, splitPairs, loc);
      return combineDegrees(join.getResult(), accumulator, elementStream, ColumnMapper(), loc);
   }
   mlir::Value combineDegrees(mlir::Value joined, mlir::Value left, mlir::Value right, const ColumnMapper& paddedRight, mlir::Location loc) {
      mlir::Value current = joined;
      llvm::SmallVector<std::pair<const tuples::Column*, bool>> combined;
      for (auto& scope : fuzzyScopes) {
         const auto* leftDegree = scope.degreeOf.lookup(left);
         const auto* rightDegree = right ? scope.degreeOf.lookup(right) : nullptr;
         if (const auto* padded = rightDegree ? paddedRight.lookup(rightDegree) : nullptr) rightDegree = padded;
         const tuples::Column* degree = leftDegree ? leftDegree : rightDegree;
         if (leftDegree && rightDegree) std::tie(current, degree) = buildProductTNorm(current, leftDegree, rightDegree, loc);
         combined.emplace_back(degree, rightDegree != nullptr);
      }
      llvm::SmallVector<bool> prunedNow;
      for (auto [scope, entry] : llvm::zip(fuzzyScopes, combined)) {
         auto [degree, contributed] = entry;
         bool prune = scope.threshold && degree && contributed;
         if (prune) current = buildThresholdSelection(current, degree, *scope.threshold, /*keepUnbound=*/true, loc);
         prunedNow.push_back(prune && !mlir::isa<db::NullableType>(degree->type));
      }
      for (auto [scope, entry, prunedHere] : llvm::zip(fuzzyScopes, combined, prunedNow)) {
         if (entry.first) scope.degreeOf[current] = entry.first;
         if (prunedHere) scope.pruned.insert(current);
      }
      return current;
   }
   std::pair<mlir::Value, const tuples::Column*> buildProductTNorm(mlir::Value stream, const tuples::Column* lhs, const tuples::Column* rhs, mlir::Location loc) {
      auto* ctxt = stream.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      mlir::Type f32 = mlir::Float32Type::get(ctxt);
      bool nullable = mlir::isa<db::NullableType>(lhs->type) && mlir::isa<db::NullableType>(rhs->type);
      auto accDef = columnManager.createDef(columnManager.getUniqueScope("fuzzy"), "acc");
      accDef.getColumn().type = nullable ? static_cast<mlir::Type>(db::NullableType::get(ctxt, f32)) : f32;
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      auto* block = new mlir::Block;
      mlir::Value tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         llvm::SmallVector<mlir::Value, 2> isNull;
         auto load = [&](const tuples::Column* column) -> mlir::Value {
            mlir::Value value = builder.create<tuples::GetColumnOp>(loc, column->type, columnManager.createRef(column), tuple);
            if (!mlir::isa<db::NullableType>(column->type)) return value;
            mlir::Value null = builder.create<db::IsNullOp>(loc, value);
            isNull.push_back(null);
            mlir::Value raw = builder.create<db::NullableGetVal>(loc, value);
            mlir::Value one = builder.create<mlir::arith::ConstantOp>(loc, f32, builder.getF32FloatAttr(1.0f));
            return builder.create<mlir::arith::SelectOp>(loc, null, one, raw);
         };
         mlir::Value lhsVal = load(lhs);
         mlir::Value rhsVal = load(rhs);
         mlir::Value product = builder.create<mlir::arith::MulFOp>(loc, lhsVal, rhsVal);
         if (nullable) {
            mlir::Value bothNull = builder.create<mlir::arith::AndIOp>(loc, isNull[0], isNull[1]);
            product = builder.create<db::AsNullableOp>(loc, accDef.getColumn().type, product, bothNull);
         }
         builder.create<tuples::ReturnOp>(loc, product);
      }
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, builder.getArrayAttr({accDef}));
      mapOp.getPredicate().push_back(block);
      return {mapOp.getResult(), &accDef.getColumn()};
   }
   mlir::Value buildThresholdSelection(mlir::Value stream, const tuples::Column* degree, float threshold, bool keepUnbound, mlir::Location loc, mlir::arith::CmpFPredicate predicate = mlir::arith::CmpFPredicate::OGE) {
      auto* ctxt = stream.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      auto selOp = builder.create<relalg::SelectionOp>(loc, tuples::TupleStreamType::get(ctxt), stream);
      auto* block = new mlir::Block;
      mlir::Value tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
      selOp.getPredicate().push_back(block);
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(block);
      mlir::Value value = builder.create<tuples::GetColumnOp>(loc, degree->type, columnManager.createRef(degree), tuple);
      mlir::Value bound = builder.create<mlir::arith::ConstantOp>(loc, builder.getF32Type(), builder.getF32FloatAttr(threshold));
      mlir::Value pred;
      if (mlir::isa<db::NullableType>(degree->type)) {
         mlir::Value null = builder.create<db::IsNullOp>(loc, value);
         mlir::Value raw = builder.create<db::NullableGetVal>(loc, value);
         mlir::Value reached = builder.create<mlir::arith::CmpFOp>(loc, predicate, raw, bound);
         mlir::Value unboundResult = builder.create<mlir::arith::ConstantIntOp>(loc, keepUnbound ? 1 : 0, 1);
         pred = builder.create<mlir::arith::SelectOp>(loc, null, unboundResult, reached);
      }
      else {
         pred = builder.create<mlir::arith::CmpFOp>(loc, predicate, value, bound);
      }
      builder.create<tuples::ReturnOp>(loc, pred);
      return selOp.getResult();
   }
   mlir::Value bindTruthDegree(mlir::Value stream, const tuples::Column* degree, tuples::ColumnDefAttr resultDef, mlir::Location loc) {
      auto* ctxt = stream.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      mlir::Type variantType = variant::VariantType::get(ctxt);
      bool nullable = mlir::isa<db::NullableType>(degree->type);
      auto truthDef = columnManager.createDef(&resultDef.getColumn());
      truthDef.getColumn().type = nullable ? static_cast<mlir::Type>(db::NullableType::get(ctxt, variantType)) : variantType;
      getOperation()->walk([&](tuples::GetColumnOp getColumnOp) {
         if (&getColumnOp.getAttr().getColumn() == &truthDef.getColumn()) gpm::BindingColumnTransformer::remapBindingReadType(getColumnOp);
      });
      mlir::OpBuilder builder(ctxt);
      builder.setInsertionPoint(insertPoint);
      auto* block = new mlir::Block;
      mlir::Value tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         mlir::Value value = builder.create<tuples::GetColumnOp>(loc, degree->type, columnManager.createRef(degree), tuple);
         mlir::Value truth;
         if (nullable) {
            mlir::Value null = builder.create<db::IsNullOp>(loc, value);
            mlir::Value raw = builder.create<db::NullableGetVal>(loc, value);
            mlir::Value scalar = builder.create<variant::CreateScalarOp>(loc, variantType, raw);
            truth = builder.create<db::AsNullableOp>(loc, truthDef.getColumn().type, scalar, null);
         }
         else {
            truth = builder.create<variant::CreateScalarOp>(loc, variantType, value);
         }
         builder.create<tuples::ReturnOp>(loc, truth);
      }
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, builder.getArrayAttr({truthDef}));
      mapOp.getPredicate().push_back(block);
      return mapOp.getResult();
   }
   relalg::ColumnSet collectTripleVariables(mlir::Value v, llvm::SmallPtrSet<mlir::Operation*, 16>& visited) {
      relalg::ColumnSet result;
      auto* op = v.getDefiningOp();
      if (!op || !visited.insert(op).second) return result;
      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op)) {
         for (auto slot : kTripleSlots) {
            auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(getSlot(triple, slot));
            if (!varTerm) continue;
            if (auto ref = varTerm.getBindingReference()) {
               result.insert(&ref.getColumn());
            }
            else if (auto source = bindingSource(varTerm)) {
               result.insert(&source.getColumn());
            }
            else {
               result.insert(&varTerm.getProducedBinding().getColumn());
            }
         }
      }
      if (auto outerJoin = mlir::dyn_cast<relalg::OuterJoinOp>(op)) {
         for (auto mappingAttr : outerJoin.getMapping()) {
            result.insert(&mlir::cast<tuples::ColumnDefAttr>(mappingAttr).getColumn());
         }
         if (auto merged = outerJoin->getAttrOfType<mlir::ArrayAttr>(gpm::kMergedColumnsAttr)) {
            for (auto mergedAttr : merged) result.insert(&mlir::cast<tuples::ColumnDefAttr>(mergedAttr).getColumn());
         }
         result.insert(collectTripleVariables(outerJoin.getLeft(), visited));
         return result;
      }
      if (auto bindMap = asBindMap(op)) insertComputedColumns(bindMap, result);
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
         llvm::DenseMap<const tuples::Column*, tuples::ColumnRefAttr> freshInTriple;
         for (auto slot : kTripleSlots) {
            auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(getSlot(triple, slot));
            if (!varTerm) continue;
            auto existingSource = bindingSource(varTerm);
            auto outerRef = existingSource ? existingSource : varTerm.getBindingReference();
            if (!outerRef) continue;
            const auto* column = &outerRef.getColumn();
            if (!needsBindings(column)) continue;

            tuples::ColumnRefAttr newRef;
            if (existingSource) {
               newRef = columnManager.createRef(varTerm.getProducedBinding().getColumnPtr().get());
            }
            else if (auto it = freshInTriple.find(column); it != freshInTriple.end()) {
               setSlot(triple, slot, gpm::VariableTermAttr::get(ctxt, it->second));
               continue;
            }
            else {
               auto fromExisting = mlir::ArrayAttr::get(ctxt, {outerRef});
               auto newDef = columnManager.createDef(columnManager.getUniqueScope("bindings"), slotName(slot), fromExisting);
               newDef.getColumn().type = producedBindingType(column->type);
               newRef = columnManager.createRef(newDef.getColumnPtr().get());
               setSlot(triple, slot, gpm::VariableTermAttr::get(ctxt, newDef));
               llvm::SmallPtrSet<mlir::Operation*, 16> visitedStale;
               fixStaleTripleColumnReferences(right, column, newRef, visitedStale);
               freshInTriple[column] = newRef;
            }
            result.push_back({outerRef, newRef});
         }
      }
      return result;
   }
   void addSharedVariablePredicate(PredicateOperator join, mlir::Value left, mlir::Value right, mlir::Location loc) {
      llvm::SmallPtrSet<mlir::Operation*, 16> visitedLeft;
      relalg::ColumnSet leftVars = collectTripleVariables(left, visitedLeft);
      addSharedVariablePredicate(join, sharedVariablePairs(right, loc, leftVars), loc);
   }
   ColumnPairs sharedVariablePairs(mlir::Value right, mlir::Location loc, const relalg::ColumnSet& leftVars) {
      auto pairs = ensureBindingsColumns(right, loc, [&](const tuples::Column* c) { return leftVars.contains(c); });
      ColumnPairs result;
      if (pairs.empty()) return result;
      ColumnMapper unionRemaps;
      {
         llvm::SmallPtrSet<mlir::Operation*, 16> visited;
         collectOuterJoinRemaps(right, visited, unionRemaps);
      }
      llvm::SmallPtrSet<const tuples::Column*, 8> seenOuterCols;
      for (auto& pair : pairs) {
         if (!seenOuterCols.insert(&pair.outerRef.getColumn()).second) continue;
         result.emplace_back(&pair.outerRef.getColumn(), resolveThroughRemaps(unionRemaps, &pair.bindingsRef.getColumn()));
      }
      return result;
   }
   void addSharedVariablePredicate(PredicateOperator join, const ColumnPairs& pairs, mlir::Location loc) {
      if (pairs.empty()) return;
      auto& columnManager = join.getOperation()->getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::SmallVector<mlir::Attribute> leftHash, rightHash;
      for (auto [outerCol, bindingsCol] : pairs) {
         auto outerRef = columnManager.createRef(outerCol);
         auto bindingsRef = columnManager.createRef(bindingsCol);
         join.addPredicate([&](mlir::Value tuple, mlir::OpBuilder& builder) -> mlir::Value {
            mlir::Value lhsVal = builder.create<tuples::GetColumnOp>(loc, outerCol->type, outerRef, tuple);
            mlir::Value rhsVal = builder.create<tuples::GetColumnOp>(loc, bindingsCol->type, bindingsRef, tuple);
            return buildCompatible(builder, loc, lhsVal, rhsVal);
         });
         leftHash.push_back(outerRef);
         rightHash.push_back(bindingsRef);
      }
      appendHashKeys(join, leftHash, rightHash);
   }
   void addNameMatchedPredicate(PredicateOperator join, const ColumnPairs& pairs, mlir::Location loc, bool recordMerges = true) {
      if (pairs.empty()) return;
      auto& columnManager = join.getOperation()->getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::SmallVector<mlir::Attribute> leftHash, rightHash;
      for (auto [leftCol, rightCol] : pairs) {
         auto leftRef = columnManager.createRef(leftCol);
         auto rightRef = columnManager.createRef(rightCol);
         join.addPredicate([&](mlir::Value tuple, mlir::OpBuilder& builder) -> mlir::Value {
            mlir::Value lhsVal = builder.create<tuples::GetColumnOp>(loc, leftCol->type, leftRef, tuple);
            mlir::Value rhsVal = builder.create<tuples::GetColumnOp>(loc, rightCol->type, rightRef, tuple);
            return buildCompatible(builder, loc, lhsVal, rhsVal);
         });
         leftHash.push_back(leftRef);
         rightHash.push_back(rightRef);
         if (recordMerges) nameMerges[rightCol] = leftCol;
      }
      appendHashKeys(join, leftHash, rightHash);
   }
   mlir::Value buildCompatible(mlir::OpBuilder& builder, mlir::Location loc, mlir::Value lhsVal, mlir::Value rhsVal) {
      return builder.create<gpm::BindingsCompatibleOp>(loc, builder.getI1Type(), lhsVal, rhsVal);
   }
   void addDomainOverlapPredicate(PredicateOperator join, const ColumnPairs& pairs, mlir::Location loc) {
      if (pairs.empty()) return;
      for (auto [leftCol, rightCol] : pairs) {
         if (!mlir::isa<db::NullableType>(leftCol->type) && !mlir::isa<db::NullableType>(rightCol->type)) return;
      }
      auto& columnManager = join.getOperation()->getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      join.addPredicate([&](mlir::Value tuple, mlir::OpBuilder& builder) -> mlir::Value {
         llvm::SmallVector<mlir::Value> overlaps;
         for (auto [leftCol, rightCol] : pairs) {
            llvm::SmallVector<mlir::Value> bothBound;
            for (const auto* col : {leftCol, rightCol}) {
               if (!mlir::isa<db::NullableType>(col->type)) continue;
               mlir::Value val = builder.create<tuples::GetColumnOp>(loc, col->type, columnManager.createRef(col), tuple);
               mlir::Value isNull = builder.create<db::IsNullOp>(loc, val);
               bothBound.push_back(builder.create<db::NotOp>(loc, isNull));
            }
            overlaps.push_back(bothBound.size() == 1 ? bothBound[0] : builder.create<db::AndOp>(loc, bothBound).getResult());
         }
         return overlaps.size() == 1 ? overlaps[0] : builder.create<db::OrOp>(loc, overlaps).getResult();
      });
   }
   void appendHashKeys(PredicateOperator join, llvm::ArrayRef<mlir::Attribute> leftKeys, llvm::ArrayRef<mlir::Attribute> rightKeys) {
      auto* ctxt = join.getOperation()->getContext();
      auto append = [&](llvm::StringRef name, llvm::ArrayRef<mlir::Attribute> added) {
         llvm::SmallVector<mlir::Attribute> values;
         if (auto existing = join->getAttrOfType<mlir::ArrayAttr>(name)) values.append(existing.begin(), existing.end());
         values.append(added.begin(), added.end());
         join->setAttr(name, mlir::ArrayAttr::get(ctxt, values));
      };
      append("leftHash", leftKeys);
      append("rightHash", rightKeys);
      // We select join strategy within the RelAlg query optimizer.
      // join->setAttr("impl", mlir::StringAttr::get(ctxt, "hash"));
      // join->setAttr("useHashJoin", mlir::UnitAttr::get(ctxt));
      append("nullsEqual", llvm::SmallVector<mlir::Attribute>(leftKeys.size(), mlir::IntegerAttr::get(mlir::IntegerType::get(ctxt, 8), 0)));
      // variable binding compatibility semantics for join operators
      append("bindingCompatible", llvm::SmallVector<mlir::Attribute>(leftKeys.size(), mlir::IntegerAttr::get(mlir::IntegerType::get(ctxt, 8), 1)));
   }
   void fixStaleTripleColumnReferences(mlir::Value subtreeRoot, const tuples::Column* oldColumn, tuples::ColumnRefAttr newRef, llvm::SmallPtrSetImpl<mlir::Operation*>& visited) {
      auto* op = subtreeRoot.getDefiningOp();
      if (!op || !visited.insert(op).second) return;
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
            gpm::BindingColumnTransformer::remapBindingReadType(getColumnOp);
         }
      });
      for (auto operand : op->getOperands()) {
         fixStaleTripleColumnReferences(operand, oldColumn, newRef, visited);
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
   mlir::ArrayAttr buildMergedColumns(const ColumnPairs& pairs, const ColumnMapper& paddedColMap) {
      auto* ctxt = insertPoint->getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto scope = columnManager.getUniqueScope("merge");
      llvm::SmallVector<mlir::Attribute> mergedCols;
      for (auto [leftCol, rightCol] : pairs) {
         const auto* paddedCol = paddedColMap.lookup(rightCol);
         auto sources = mlir::ArrayAttr::get(ctxt, {columnManager.createRef(leftCol), columnManager.createRef(paddedCol)});
         auto mergedDef = columnManager.createDef(scope, columnManager.getName(leftCol).second, sources);
         mergedDef.getColumn().type = leftCol->type;
         mergedCols.push_back(mergedDef);
      }
      return mlir::ArrayAttr::get(ctxt, mergedCols);
   }
   void collectSubtreeOps(mlir::Value v, llvm::SmallPtrSetImpl<mlir::Operation*>& ops) {
      auto* op = v.getDefiningOp();
      if (!op || ops.contains(op)) return;
      op->walk([&](mlir::Operation* nested) { ops.insert(nested); });
      for (auto operand : op->getOperands()) collectSubtreeOps(operand, ops);
   }
   void bnodesToVariables(gpm::TriplePatternOp triple, llvm::StringMap<tuples::ColumnRefAttr>& scope) {
      auto* ctxt = triple.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      for (auto slot : kTripleSlots) {
         auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(getSlot(triple, slot));
         if (!bnode) continue;
         auto localId = bnode.getLocalId().getValue();
         auto it = scope.find(localId);
         mlir::Attribute binding;
         if (it == scope.end()) {
            auto def = columnManager.createDef(columnManager.getUniqueScope("bnode"), localId);
            def.getColumn().type = variant::VariantType::get(ctxt);
            scope[localId] = columnManager.createRef(def.getColumnPtr().get());
            binding = def;
         }
         else {
            binding = it->second;
         }
         setSlot(triple, slot, gpm::VariableTermAttr::get(ctxt, binding));
      }
   }
   void normalizeTriples() {
      auto* ctxt = &getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      getOperation()->walk([&](gpm::TriplePatternOp triple) {
         for (auto slot : kTripleSlots) {
            auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(getSlot(triple, slot));
            if (!varTerm) continue;
            const tuples::Column* column;
            if (auto ref = varTerm.getBindingReference()) {
               column = &ref.getColumn();
            }
            else if (bindingSource(varTerm)) {
               column = &varTerm.getProducedBinding().getColumn();
            }
            else {
               continue;
            }
            setSlot(triple, slot, gpm::VariableTermAttr::get(ctxt, columnManager.createDef(column)));
         }
      });
   }
};

} // namespace

std::unique_ptr<mlir::Pass> gpm::createUnnestGraphPatternsPass() { return std::make_unique<UnnestGraphPatternsPass>(); }
