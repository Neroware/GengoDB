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
using BlankNodeMapping = llvm::DenseMap<Operation*, std::shared_ptr<LocalIdentifierMapping>>;
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
inline static std::string getMemberName(std::string group, std::string graph, std::string suffix = "") {
   return group + "_" + graph + (suffix.empty() ? "" : "_" + suffix);
}
template<typename GSetType>
inline static GSetType createGraphSetType(MLIRContext* ctxt, std::string group, std::string name, std::string suffix = "", std::string itStrategy = "all") {
   auto itType = gsubop::GraphSetIteratorType::get(ctxt, mlir::ArrayAttr::get(ctxt, {StringAttr::get(ctxt, itStrategy)}));
   auto itMember = createMember(ctxt, getMemberName(group, name, suffix + "_it"), itType);
   return GSetType::get(ctxt, createStateMembersAttr(ctxt, {itMember}));
}
template<typename GSetType>
inline static GSetType getGraphSetType(std::string group, std::string name, tuples::ColumnManager& columnManager, std::string suffix = "") {
   auto col = columnManager.get(group, name + (suffix.empty() ? "" : "_" + suffix)).get();
   return mlir::cast<GSetType>(col->type);
}
inline static tuples::ColumnDefAttr createDef(std::string group, std::string name, mlir::Type type, tuples::ColumnManager& columnManager, std::string suffix = "") {
   auto def = columnManager.createDef(group, name + (suffix.empty() ? "" : "_" + suffix));
   def.getColumn().type = type;
   return def;
}
inline static tuples::ColumnRefAttr createRef(std::string group, std::string name, tuples::ColumnManager& columnManager, std::string suffix = "") {
   auto col = columnManager.get(group, name + (suffix.empty() ? "" : "_" + suffix));
   return columnManager.createRef(col.get());
}
inline static gsubop::NodeRefType createNodeRefType(MLIRContext* ctxt, std::string group, std::string name) {
   auto edgeSetType0 = createGraphSetType<gsubop::EdgeSetType>(ctxt, group, name, "incoming", "incoming");
   auto edgeSetType1 = createGraphSetType<gsubop::EdgeSetType>(ctxt, group, name, "outgoing", "outgoing");
   auto propSetType = createGraphSetType<gsubop::PropertySetType>(ctxt, group, name, "node", "node");
   auto nodeId = createMember(ctxt, getMemberName(group, name, "node"), mlir::IntegerType::get(ctxt, 32));
   auto incoming = createMember(ctxt, getMemberName(group, name, "incoming"), edgeSetType0);
   auto outgoing = createMember(ctxt, getMemberName(group, name, "outgoing"), edgeSetType1);
   auto property = createMember(ctxt, getMemberName(group, name, "property"), propSetType);
   return gsubop::NodeRefType::get(ctxt, 
      createStateMembersAttr(ctxt, {nodeId}), 
      createStateMembersAttr(ctxt, {incoming}), 
      createStateMembersAttr(ctxt, {outgoing}), 
      createStateMembersAttr(ctxt, {property})
   );
}
inline static bool isBound(mlir::Attribute term, const LocalIdentifierMapping& localIdents) {
   if (mlir::isa<gpm::IdentifierTermAttr>(term)) return true;
   if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
      return var.hasBinding();
   if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term))
      return localIdents.count(bnode.getLocalId()) > 0;
   return false;
}

class NamedGraphLowering : public OpConversionPattern<gpm::NamedGraphOp> {
   public:
   NamedGraphLowering(TypeConverter& typeConverter, MLIRContext* context)
      : OpConversionPattern<gpm::NamedGraphOp>(typeConverter, context) {}
   LogicalResult matchAndRewrite(gpm::NamedGraphOp namedGraphOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto ctxt = rewriter.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      std::string graphGroup = namedGraphOp.getDef().getName().getRootReference().str();
      std::string graphName = namedGraphOp.getDef().getName().getLeafReference().str();
      auto graphNameAttr = StringAttr::get(ctxt, graphName);
      auto nodeSetType = createGraphSetType<gsubop::NodeSetType>(ctxt, graphGroup, graphName, "vx");
      auto edgeSetType = createGraphSetType<gsubop::EdgeSetType>(ctxt, graphGroup, graphName, "ex");
      auto nodeSetMember = createMember(ctxt, getMemberName(graphGroup, graphName, "vx"), nodeSetType);
      auto edgeSetMember = createMember(ctxt, getMemberName(graphGroup, graphName, "ex"), edgeSetType);
      auto graphType = gsubop::GraphType::get(ctxt, createStateMembersAttr(ctxt, {nodeSetMember}), createStateMembersAttr(ctxt, {edgeSetMember}));
      auto nodeSetDef = createDef(graphGroup, graphName, nodeSetType, columnManager, "nodes");
      auto edgeSetDef = createDef(graphGroup, graphName, edgeSetType, columnManager, "edges");
      mlir::Value graphRef = rewriter.create<gsubop::GetExternalGraphOp>(namedGraphOp->getLoc(), graphType, graphNameAttr, namedGraphOp.getGraph());
      rewriter.replaceOpWithNewOp<gsubop::ScanGraphOp>(namedGraphOp, graphRef, nodeSetDef, edgeSetDef);
      // graphRef.getDefiningOp()->getParentOfType<mlir::ModuleOp>().dump();
      return success();
   }
};

class BasicGraphPatternLowering : public OpConversionPattern<gpm::BasicGraphPatternOp> {
   BlankNodeMapping& blankNodes;
   public:
   BasicGraphPatternLowering(TypeConverter& typeConverter, MLIRContext* context, BlankNodeMapping& blankNodes)
      : OpConversionPattern<gpm::BasicGraphPatternOp>(typeConverter, context), blankNodes(blankNodes) {}
   LogicalResult matchAndRewrite(gpm::BasicGraphPatternOp basicGraphPatternOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto& block = basicGraphPatternOp.getPattern().front();
      IRMapping mapping;
      auto localIdents = std::make_shared<LocalIdentifierMapping>();
      if (!block.getArguments().empty()) {
         mapping.map(block.getArguments()[0], adaptor.getRel());
      }
      rewriter.setInsertionPoint(basicGraphPatternOp);
      mlir::Value lastResult = adaptor.getRel();
      SmallVector<Operation*> ops;
      for (auto& op : block.without_terminator()) {
         ops.push_back(&op);
      }
      for (auto* op : ops) {
         auto* newOp = rewriter.clone(*op, mapping);
         blankNodes.insert(std::make_pair(newOp, localIdents));
         for (auto [origRes, newRes] : llvm::zip(op->getResults(), newOp->getResults())) {
            mapping.map(origRes, newRes);
         }
         if (newOp->getNumResults() > 0) {
            lastResult = newOp->getResult(0);
         }
      }
      rewriter.replaceOp(basicGraphPatternOp, lastResult);
      basicGraphPatternOp->getParentOfType<ModuleOp>().dump();
      return success();
   }
};

class TriplePatternLowering : public OpConversionPattern<gpm::TriplePatternOp> {
   const BlankNodeMapping& blankNodes;
   public:
   TriplePatternLowering(TypeConverter& typeConverter, MLIRContext* context, const BlankNodeMapping& blankNodes)
      : OpConversionPattern<gpm::TriplePatternOp>(typeConverter, context), blankNodes(blankNodes) {}
   LogicalResult matchAndRewrite(gpm::TriplePatternOp triplePatternOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto it = blankNodes.find(triplePatternOp.getOperation());
      if (it == blankNodes.end()) return failure();
      auto& localIdents = *it->second;
      auto loc = triplePatternOp.getLoc();
      auto& columnManager = rewriter.getContext()
         ->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      mlir::Value stream = adaptor.getRel();
      // std::string graphGroup = triplePatternOp.getGraphRef().getName().getRootReference().str();
      // std::string graphName = triplePatternOp.getGraphRef().getName().getLeafReference().str();
      // auto nodesRef = createRef(graphGroup, graphName, columnManager, "nodes");
      // auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(rewriter.getContext()), stream, rewriter.getArrayAttr({nodesRef}));
      // auto* b = new Block();
      // b->addArgument(tuples::TupleType::get(rewriter.getContext()), loc);
      // auto nodeSetType = getGraphSetType<gsubop::NodeSetType>(graphGroup, graphName, columnManager, "nodes");
      // auto nodeSetArg = b->addArgument(nodeSetType, loc);
      // nestedMapOp.getRegion().push_back(b);
      // {
      //    mlir::OpBuilder::InsertionGuard guard(rewriter);
      //    rewriter.setInsertionPointToStart(b);
      //    auto nodeRefType = createNodeRefType(triplePatternOp.getContext(), graphGroup, graphName);
      //    mlir::Value scan = rewriter.create<gsubop::ScanNodeSetOp>(loc, nodeSetArg, createDef(graphGroup, graphName, nodeRefType, columnManager, "noderef"));
      //    rewriter.create<tuples::ReturnOp>(loc, scan);
      // }
      // nestedMapOp->getParentOfType<ModuleOp>().dump();
      if (isBound(triplePatternOp.getS(), localIdents))
         stream = lowerSubjectFirst(rewriter, loc, stream, triplePatternOp, localIdents, columnManager);
      else if (isBound(triplePatternOp.getO(), localIdents))
         return failure();
      return failure();
   }
   private:
   mlir::Value lowerSubjectFirst(ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp op, LocalIdentifierMapping& localIdents, tuples::ColumnManager& columnManager) const {
      mlir::Value subject;
      auto ctxt = op.getContext();
      if (auto identTermAttr = mlir::dyn_cast_or_null<gpm::IdentifierTermAttr>(op.getS())) {
         std::string group = op.getGraphRef().getName().getRootReference().str();
         std::string graph = op.getGraphRef().getName().getLeafReference().str();
         auto nodesRef = createRef(group, graph, columnManager, "nodes");
         auto identMember = createMember(ctxt, "lookupIdent", gsubop::IdentifierType::get(ctxt));
         auto identStateType = SimpleStateType::get(ctxt, createStateMembersAttr(ctxt, {identMember}));
         auto identState = rewriter.create<gsubop::CreateIdentifierStateOp>(loc, identStateType, identTermAttr.getIdent());
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef}));
         auto* b = new Block();
         b->addArgument(tuples::TupleType::get(ctxt), loc);
         auto nodeSetType = getGraphSetType<gsubop::NodeSetType>(group, graph, columnManager, "nodes");
         auto nodeSetArg = b->addArgument(nodeSetType, loc);
         nestedMapOp.getRegion().push_back(b);
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(b);
            auto identDef = createDef("idents", "lookup", gsubop::IdentifierType::get(ctxt), columnManager);
            auto scan = rewriter.create<subop::ScanRefsOp>(loc, identState, identDef);
            auto ref = columnManager.createRef(identDef.getColumnPtr().get());
            auto nodeRefType = createNodeRefType(ctxt, group, graph);
            auto def = createDef(group, graph, nodeRefType, columnManager, "noderef");
            mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({ref}), def);
            rewriter.create<tuples::ReturnOp>(loc, lookup);
            nestedMapOp->getParentOfType<ModuleOp>().dump();
         }
      }
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
