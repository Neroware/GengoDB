#include "gengodb/compiler/Conversion/GPMToSubOp/GPMToSubOpPass.h"

#include "lingodb/compiler/Conversion/RelAlgToSubOp/OrderedAttributes.h"
#include "lingodb/compiler/Dialect/Arrow/IR/ArrowDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "lingodb/compiler/Dialect/SubOperator/Utils.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"
#include "lingodb/compiler/Dialect/util/FunctionHelper.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Async/IR/Async.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/ADT/TypeSwitch.h"

#include <iostream>

using namespace mlir;

namespace {
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;
using Member = subop::Member;
using LocalIdentifierMapping = llvm::DenseMap<mlir::StringRef, mlir::Value>;
using BlankNodeMapping = llvm::DenseMap<gpm::BasicGraphPatternOp, LocalIdentifierMapping>;
struct GPMToSubOpLoweringPass
   : public PassWrapper<GPMToSubOpLoweringPass, OperationPass<ModuleOp>> {
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GPMToSubOpLoweringPass)
   virtual llvm::StringRef getArgument() const override { return "to-graph-subop"; }

   GPMToSubOpLoweringPass() {}
   void getDependentDialects(DialectRegistry& registry) const override {
      registry.insert<LLVM::LLVMDialect, db::DBDialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithDialect, gpm::GPMDialect, subop::SubOperatorDialect>();
   }
   void runOnOperation() final;
};
// static std::string extractIriTail(llvm::StringRef iri) {
//    iri = iri.split('#').first;
//    iri = iri.split('?').first;
//    auto slash = iri.rfind('/');
//    if (slash != llvm::StringRef::npos) {
//       iri = iri.drop_front(slash + 1);
//    }
//    auto dot = iri.rfind('.');
//    if (dot != llvm::StringRef::npos && dot != 0) {
//       iri = iri.take_front(dot);
//    }
//    return iri.str();
// }
static Member createMember(MLIRContext* context, std::string name, mlir::Type type) {
   auto& memberManager = context->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
   return memberManager.createMember(name, type);
}
static subop::StateMembersAttr createStateMembersAttr(MLIRContext* context, const llvm::SmallVector<Member>& members) {
   return subop::StateMembersAttr::get(context, members);
}

static relalg::ColumnSet getRequired(Operator op, llvm::DenseMap<Operator, relalg::ColumnSet>& requiredCols, relalg::AvailabilityCache& cache) {
   if (requiredCols.count(op)) {
      return requiredCols[op];
   }
   auto available = op.getAvailableColumns(cache);

   relalg::ColumnSet required;
   for (auto* user : op->getUsers()) {
      if (auto consumingOp = mlir::dyn_cast_or_null<Operator>(user)) {
         required.insert(getRequired(consumingOp, requiredCols, cache));
         required.insert(consumingOp.getUsedColumns());
      }
   }
   auto res = available.intersect(required);
   requiredCols.insert({op, res});
   return res;
}

class NamedGraphLowering : public OpConversionPattern<gpm::NamedGraphOp> {
   public:
   NamedGraphLowering(TypeConverter& typeConverter, MLIRContext* context)
      : OpConversionPattern<gpm::NamedGraphOp>(typeConverter, context) {}
   LogicalResult matchAndRewrite(gpm::NamedGraphOp namedGraphOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto ctxt = rewriter.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      
      std::string graphName = namedGraphOp.getDef().getName().getLeafReference().str();
      auto graphNameAttr = StringAttr::get(ctxt, graphName);

      auto allItType = gsubop::GraphSetIteratorType::get(ctxt, mlir::ArrayAttr::get(ctxt, {StringAttr::get(ctxt, "all")}));
      auto nodeSetItMember = createMember(ctxt, graphName + "_vx_it", allItType);
      auto nodeSetType = gsubop::NodeSetType::get(ctxt, createStateMembersAttr(ctxt, {nodeSetItMember}));
      auto edgeSetItMember = createMember(ctxt, graphName + "_ex_it", allItType);
      auto edgeSetType = gsubop::EdgeSetType::get(ctxt, createStateMembersAttr(ctxt, {edgeSetItMember}));
      auto nodeSetMember = createMember(ctxt, graphName + "_vx", nodeSetType);
      auto edgeSetMember = createMember(ctxt, graphName + "_ex", edgeSetType);
      auto graphType = gsubop::GraphType::get(ctxt, createStateMembersAttr(ctxt, {nodeSetMember}), createStateMembersAttr(ctxt, {edgeSetMember}));
      
      auto nodeSetDef = columnManager.createDef(graphName, "nodes");
      nodeSetDef.getColumn().type = nodeSetType;
      auto edgeSetDef = columnManager.createDef(graphName, "edges");
      edgeSetDef.getColumn().type = edgeSetType;
      mlir::Value graphRef = rewriter.create<gsubop::GetExternalGraphOp>(namedGraphOp->getLoc(), graphType, graphNameAttr, namedGraphOp.getGraph());
      rewriter.replaceOpWithNewOp<gsubop::ScanGraphOp>(namedGraphOp, graphRef, nodeSetDef, edgeSetDef);
      // graphRef.getDefiningOp()->getParentOfType<mlir::ModuleOp>().dump();
      return success();
   }
};

class BasicGraphPatternLowering : public OpConversionPattern<gpm::BasicGraphPatternOp> {
   const BlankNodeMapping& blankNodes;
   public:
   BasicGraphPatternLowering(TypeConverter& typeConverter, MLIRContext* context, const BlankNodeMapping& blankNodes)
      : OpConversionPattern<gpm::BasicGraphPatternOp>(typeConverter, context), blankNodes(blankNodes) {}
   LogicalResult matchAndRewrite(gpm::BasicGraphPatternOp basicGraphPatternOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      return failure();
   }
};

class TriplePatternLowering : public OpConversionPattern<gpm::TriplePatternOp> {
   const BlankNodeMapping& blankNodes;
   public:
   TriplePatternLowering(TypeConverter& typeConverter, MLIRContext* context, const BlankNodeMapping& blankNodes)
      : OpConversionPattern<gpm::TriplePatternOp>(typeConverter, context), blankNodes(blankNodes) {}
   LogicalResult matchAndRewrite(gpm::TriplePatternOp triplePatternOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      return failure();
   }
};

void GPMToSubOpLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);

   llvm::DenseMap<Operator, relalg::ColumnSet> requiredColumns;

   relalg::AvailabilityCache availabilityCache;
   getOperation().walk([&](Operator op) {
      requiredColumns[op] = getRequired(op, requiredColumns, availabilityCache);
   });

   // Define Conversion Target
   ConversionTarget target(getContext());
   target.addLegalDialect<gpu::GPUDialect>();
   target.addLegalDialect<async::AsyncDialect>();
   target.addLegalOp<ModuleOp>();
   target.addLegalOp<UnrealizedConversionCastOp>();
   target.addIllegalDialect<gpm::GPMDialect>();
   target.addLegalDialect<subop::SubOperatorDialect>();
   target.addLegalDialect<gsubop::GraphSubOpDialect>();
   target.addLegalDialect<db::DBDialect>();
   target.addLegalDialect<lingodb::compiler::dialect::arrow::ArrowDialect>();

   target.addLegalDialect<tuples::TupleStreamDialect>();
   target.addLegalDialect<func::FuncDialect>();
   target.addLegalDialect<memref::MemRefDialect>();
   target.addLegalDialect<arith::ArithDialect>();
   target.addLegalDialect<cf::ControlFlowDialect>();
   target.addLegalDialect<scf::SCFDialect>();
   target.addLegalDialect<util::UtilDialect>();

   TypeConverter typeConverter;
   typeConverter.addConversion([](tuples::TupleStreamType t) { return t; });
   auto* ctxt = &getContext();
   ctxt->loadDialect<gsubop::GraphSubOpDialect>();
   RewritePatternSet patterns(ctxt);

   BlankNodeMapping blankNodes;

   patterns.insert<NamedGraphLowering>(typeConverter, ctxt);
   patterns.insert<BasicGraphPatternLowering>(typeConverter, ctxt, blankNodes);
   patterns.insert<TriplePatternLowering>(typeConverter, ctxt, blankNodes);

   if (failed(applyFullConversion(module, target, std::move(patterns))))
      signalPassFailure();
}
} // namespace
std::unique_ptr<mlir::Pass>
gpm::createLowerToSubOpPass() {
   return std::make_unique<GPMToSubOpLoweringPass>();
}
void gpm::createLowerGPMToSubOpPipeline(mlir::OpPassManager& pm) {
   pm.addPass(gpm::createLowerToSubOpPass());
}
void gpm::registerGPMToSubOpConversionPasses() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gpm::createLowerToSubOpPass();
   });
   mlir::PassPipelineRegistration<EmptyPipelineOptions>(
      "lower-gpm-to-subop",
      "",
      gpm::createLowerGPMToSubOpPipeline);
}
