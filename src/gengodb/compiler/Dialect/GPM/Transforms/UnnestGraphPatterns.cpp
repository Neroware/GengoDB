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
            annotateBNodeScope(triple, bnodeScope);
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
