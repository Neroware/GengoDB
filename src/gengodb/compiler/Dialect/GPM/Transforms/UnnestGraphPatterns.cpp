#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

// This pass is the boilerplate/outline for turning nested gpm.basic_graph_pattern /
// gpm.optional_graph_pattern regions into a single flat sequence of gpm.triple_pattern
// operators (a "pipeline") wired directly into the surrounding tuple stream - the same way
// e.g. relalg operators are chained. The triple pattern itself becomes the relational
// operator; the pattern-container ops (implementing GraphPatternOp) disappear after this pass.
//
// Motivation (see discussion): lowering BGPs by recursively walking their nested regions (as
// GPMToSubOp currently does) means the dialect-conversion driver never gets to dispatch on the
// individual triple patterns directly, and every new pattern construct (OPTIONAL, MINUS, ...)
// would need its own recursive-lowering special case nested inside the others. Unnesting
// up front instead gives a single, flat, iteratively-lowerable list of triple-pattern
// operators (each carrying its own semantic info: join strategy, pattern kind (BASIC/OPTIONAL/
// MINUS), bound variables), which is unnested exactly once here and never nested again
// downstream.
//
// This pass must run before gpm-create-relalg-inflights (i.e. at the very front of
// gpm::createLowerGPMToSubOpPipeline): relalg::InFlightOp insertion assumes a flat operator
// chain and does not know how to look inside a GraphPatternOp's nested region.
//
// Planned algorithm (not yet implemented - this pass is currently a no-op walk):
//   1. Collect all "root" GraphPatternOp instances, i.e. those whose parent op is not itself a
//      GraphPatternOp (nested patterns are handled recursively from their root, see below).
//   2. For each root, unnest bottom-up / post-order:
//        a. For a nested GraphPatternOp child, recurse first, so it has already been reduced to
//           a flat chain of TriplePatternOps before its parent processes it. Propagate the
//           child's getPatternKind() (OPTIONAL/MINUS in particular) down onto each of its
//           triples - e.g. via a to-be-added PatternKind attribute on TriplePatternOp, since
//           pattern-kind semantics are per-triple-pattern once unnested, not per-container. A
//           nested pattern whose ancestor is itself OPTIONAL/MINUS stays tagged as such (the
//           outermost non-BASIC ancestor wins).
//        b. For a TriplePatternOp child, just move/rewire it into the flat chain, connecting its
//           $rel operand to the last flattened stream so far.
//        c. Track bound-variable info while flattening: a variable that is *created* by an
//           earlier triple in program order and *used* again later must be turned into a bound
//           reference (mirrors what TriplePatternLowering currently does implicitly via nested
//           NestedMapOp scoping in GPMToSubOp.cpp).
//   3. Replace all uses of the root GraphPatternOp's result with the final flattened stream and
//      erase the now-empty pattern container op(s).
//
// Once this pass produces a flat chain of TriplePatternOps directly, GPMToSubOp's lowering can
// become a simple per-op OpConversionPattern<TriplePatternOp> (like any other relational
// operator), instead of the current recursive per-BGP-region walk.

namespace {
using namespace gengodb::compiler::dialect;

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
         unnest(root);
      }
   }

   private:
   // TODO: flatten `root` (and, recursively, any GraphPatternOp nested inside it) into a single
   // chain of gpm::TriplePatternOp operators as outlined above, then erase the pattern
   // container op(s). Currently a no-op so that wiring this pass into the pipeline is safe.
   void unnest(GraphPatternOp root) {
      (void) root;
   }
};

} // namespace

std::unique_ptr<mlir::Pass> gpm::createUnnestGraphPatternsPass() { return std::make_unique<UnnestGraphPatternsPass>(); }
