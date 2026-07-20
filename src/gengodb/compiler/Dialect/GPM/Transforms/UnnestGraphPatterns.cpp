#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

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
      for (auto root : roots) {
         unnestInto(root, root.getOperation(), gpm::PatternKind::basic);
      }
   }

   private:
   mlir::Value unnestInto(GraphPatternOp patternOp, mlir::Operation* insertBefore, gpm::PatternKind inheritedKind) {
      gpm::PatternKind kind = inheritedKind != gpm::PatternKind::basic ? inheritedKind : patternOp.getPatternKind();
      llvm::StringMap<tuples::ColumnRefAttr> bnodeScope;
      mlir::Value stream = patternOp.getRel();
      for (auto& op : llvm::make_early_inc_range(patternOp.getPattern().front().without_terminator())) {
         if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(&op)) {
            auto newlyProducedBNodes = annotateBNodeScope(triple, bnodeScope);
            annotateModifier(triple, kind);
            annotateNullableColumns(triple, kind, newlyProducedBNodes);
            triple.getRelMutable().set(stream);
            triple->moveBefore(insertBefore);
            stream = triple.getRes();
         }
         else if (auto nested = mlir::dyn_cast<GraphPatternOp>(&op)) {
            stream = unnestInto(nested, insertBefore, kind);
         }
         else {
            op.emitOpError("unnesting of this operator nested inside a graph pattern is not supported");
            signalPassFailure();
            return stream;
         }
      }
      mlir::Operation* rawOp = patternOp.getOperation();
      rawOp->getResult(0).replaceAllUsesWith(stream);
      rawOp->erase();
      return stream;
   }
   // Stamps `triple` with which graph pattern construct it originates from - blank (defaulting to
   // `basic` via TriplePatternOp::getModifier()) unless it is (transitively) nested inside a
   // non-basic GraphPatternOp, i.e. iff it actually needs outer-join treatment downstream. `kind`
   // is already resolved by the caller via the "outermost non-basic ancestor wins" rule, so this
   // is a direct, unconditional stamp - never applied to triples outside that scope.
   void annotateModifier(gpm::TriplePatternOp triple, gpm::PatternKind kind) {
      if (kind == gpm::PatternKind::basic) return;
      triple->setAttr("modifier", gpm::PatternKindAttr::get(triple.getContext(), kind));
   }
   // Blank nodes carry only a bare local-id string (no def/ref slot like VariableTermAttr), so
   // this pass is what resolves their scope: `scope` tracks, per *original* GraphPatternOp
   // container, which column already stands for a given local id. Returns the refs for ids seen
   // here for the first time - i.e. the columns this triple itself is responsible for producing -
   // for annotateNullableColumns() to fold into the triple's own "nullable" set alongside any
   // variable it produces.
   llvm::SmallVector<tuples::ColumnRefAttr, 2> annotateBNodeScope(gpm::TriplePatternOp triple, llvm::StringMap<tuples::ColumnRefAttr>& scope) {
      auto* ctxt = triple.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::StringMap<tuples::ColumnRefAttr> usedHere;
      llvm::SmallVector<tuples::ColumnRefAttr, 2> newlyProduced;
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
            newlyProduced.push_back(ref);
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
      return newlyProduced;
   }
   // Lists, as a `nullableColumns` array attribute, exactly the columns *this* triple is
   // responsible for producing (a newly-bound variable, or the first sighting of a blank node in
   // its scope) that read as null once this triple's optional/minus block fails to match. Reused
   // (not produced) terms need no entry of their own: nullability is self-describing from that
   // point on via the shared column's own type, once a later lowering pass wraps it in
   // `db.nullable<...>` at the point of production - a reuse site downstream just needs to see
   // that type, not a repeated attribute. Absent (rather than empty) whenever `kind` is `basic`
   // or this triple happens to produce nothing of its own, to keep the common-case IR uncluttered.
   void annotateNullableColumns(gpm::TriplePatternOp triple, gpm::PatternKind kind, llvm::ArrayRef<tuples::ColumnRefAttr> newlyProducedBNodes) {
      if (kind == gpm::PatternKind::basic) return;
      auto* ctxt = triple.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      llvm::SmallVector<mlir::Attribute> nullable;
      for (mlir::Attribute term : {triple.getS(), triple.getP(), triple.getO()}) {
         auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term);
         if (var && !var.hasBinding())
            nullable.push_back(columnManager.createRef(var.getProducedBinding().getName()));
      }
      for (auto ref : newlyProducedBNodes)
         nullable.push_back(ref);
      if (!nullable.empty())
         triple->setAttr("nullableColumns", mlir::ArrayAttr::get(ctxt, nullable));
   }
};

} // namespace

std::unique_ptr<mlir::Pass> gpm::createUnnestGraphPatternsPass() { return std::make_unique<UnnestGraphPatternsPass>(); }
