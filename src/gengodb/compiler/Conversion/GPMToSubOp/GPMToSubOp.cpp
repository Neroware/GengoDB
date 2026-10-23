#include "gengodb/compiler/Conversion/GPMToSubOp/GPMToSubOpPass.h"

#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
#include "lingodb/compiler/Dialect/Arrow/IR/ArrowDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgDialect.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"
#include "lingodb/compiler/Conversion/RelAlgToSubOp/OrderedAttributes.h"
#include "gengodb/compiler/Conversion/GPMToSubOp/TripleEmitter.h"

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
using namespace gengodb::compiler::dialect::gpm::lowering;
using namespace gengodb::compiler::dialect::gpm::lowering::helpers;
using Member = subop::Member;
using DefMappingCollector = llvm::SmallVector<subop::DefMappingPairT>;
struct ExternalGraph {
   gsubop::GraphType graphType;
   gsubop::NodeSetType nodeSetType;
   gsubop::EdgeSetType edgeSetType;
   mlir::Value externalGraph;
};
using GraphIdentity = std::pair<mlir::StringAttr, mlir::StringAttr>;
using ExternalGraphMapper = llvm::DenseMap<GraphIdentity, ExternalGraph>;
struct GPMToSubOpLoweringPass
   : public PassWrapper<GPMToSubOpLoweringPass, OperationPass<ModuleOp>> {
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GPMToSubOpLoweringPass)
   virtual llvm::StringRef getArgument() const override { return "to-graph-subop"; }

   GPMToSubOpLoweringPass() {}
   void getDependentDialects(DialectRegistry& registry) const override {
      registry.insert<LLVM::LLVMDialect, db::DBDialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithDialect, gpm::GPMDialect, subop::SubOperatorDialect, gsubop::GraphSubOpDialect, variant::VariantDialect>();
   }
   void runOnOperation() final;
};
struct GPMScalarToSubOpLoweringPass
   : public PassWrapper<GPMScalarToSubOpLoweringPass, OperationPass<ModuleOp>> {
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GPMScalarToSubOpLoweringPass)
   virtual llvm::StringRef getArgument() const override { return "to-graph-subop-scalars"; }

   GPMScalarToSubOpLoweringPass() {}
   void getDependentDialects(DialectRegistry& registry) const override {
      registry.insert<LLVM::LLVMDialect, db::DBDialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithDialect, gpm::GPMDialect, subop::SubOperatorDialect, gsubop::GraphSubOpDialect, variant::VariantDialect>();
   }
   void runOnOperation() final;
};

template<typename ColumnAttrT>
mlir::Value scanNamedGraph(ConversionPatternRewriter& rewriter, mlir::Location loc, ColumnAttrT graphAttr, NamedGraphMapper& graphs, ExternalGraphMapper& externalGraphs, bool uniqueScope = false) {
   auto ctxt = rewriter.getContext();
   auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto graphRef = graphAttr.getName();
   auto [graphGroup, graphName] = splitGraphRef(graphAttr);
   auto graphRefType = mlir::cast<gpm::GraphReferenceType>(graphAttr.getColumn().type);
   auto graphNameAttr = graphRefType.getName();
   GraphIdentity identity{graphNameAttr, graphRefType.getGlobalId()};
   auto idIt = externalGraphs.find(identity);
   if (idIt == externalGraphs.end()) {
      auto nodeSetType = createGraphSetType<gsubop::NodeSetType>(ctxt, graphGroup, graphName, "vx");
      auto edgeSetType = createGraphSetType<gsubop::EdgeSetType>(ctxt, graphGroup, graphName, "ex");
      auto nodeSetMember = createMember(ctxt, memberName(graphName, graphGroup, "vx"), nodeSetType);
      auto edgeSetMember = createMember(ctxt, memberName(graphName, graphGroup, "ex"), edgeSetType);
      auto graphType = gsubop::GraphType::get(ctxt, createStateMembersAttr(ctxt, {nodeSetMember}), createStateMembersAttr(ctxt, {edgeSetMember}));
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(rewriter.getInsertionBlock());
      auto externalGraph = rewriter.create<gsubop::GetExternalGraphOp>(loc, graphType, graphNameAttr, graphRefType.getGlobalId());
      idIt = externalGraphs.insert({identity, ExternalGraph{graphType, nodeSetType, edgeSetType, externalGraph}}).first;
   }
   auto& data = idIt->second;
   auto nodeSetDef = createDef(columnManager, graphGroup, graphName + "_vx", data.nodeSetType, uniqueScope);
   auto edgeSetDef = createDef(columnManager, graphGroup, graphName + "_ex", data.edgeSetType, uniqueScope);
   graphs.insert({graphRef, NamedGraph{data.graphType, &nodeSetDef.getColumn(), &edgeSetDef.getColumn(), data.externalGraph}});
   return rewriter.create<gsubop::ScanGraphOp>(loc, data.externalGraph, nodeSetDef, edgeSetDef);
}
static bool isRelAlgOperator(mlir::Operation* op) {
   return op && !mlir::isa<GPMOperator>(op) && mlir::isa<Operator>(op);
}

class NamedGraphLowering : public OpConversionPattern<gpm::NamedGraphOp> {
   NamedGraphMapper& graphs;
   ExternalGraphMapper& externalGraphs;
   public:
   NamedGraphLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapper& graphs, ExternalGraphMapper& externalGraphs)
      : OpConversionPattern<gpm::NamedGraphOp>(typeConverter, context), graphs(graphs), externalGraphs(externalGraphs) {}
   LogicalResult matchAndRewrite(gpm::NamedGraphOp namedGraphOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.replaceOp(namedGraphOp, scanNamedGraph(rewriter, namedGraphOp->getLoc(), namedGraphOp.getDef(), graphs, externalGraphs, true));
      return success();
   }
};
class TriplePatternLowering : public OpConversionPattern<gpm::TriplePatternOp> {
   NamedGraphMapper& graphs;
   public:
   TriplePatternLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapper& graphs)
      : OpConversionPattern<gpm::TriplePatternOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::TriplePatternOp tripleOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      bool normalized = llvm::all_of(mlir::ArrayRef<mlir::Attribute>{tripleOp.getS(), tripleOp.getP(), tripleOp.getO()}, [](mlir::Attribute term) {
         if (mlir::isa<gpm::IdentifierTermAttr>(term)) return true;
         auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term);
         return varTerm && varTerm.getProducedBinding();
      });
      if (!normalized) {
         return tripleOp.emitOpError("triple pattern is not normalized (expected only identifiers and column defs); run gpm-unnest-patterns first");
      }
      bool needsInFlight = llvm::any_of(tripleOp.getRes().getUses(), [](mlir::OpOperand& use) {
         return isRelAlgOperator(use.getOwner());
      });
      mlir::ArrayAttr availableColumns;
      if (needsInFlight) {
         relalg::AvailabilityCache availabilityCache;
         availableColumns = mlir::cast<Operator>(tripleOp.getOperation()).getAvailableColumns(availabilityCache).asRefArrayAttr(rewriter.getContext());
      }
      TripleEmitter emitter(rewriter, graphs, tripleOp);
      mlir::Value lowered = emitter.lower(adaptor.getRel());
      if (needsInFlight) {
         auto inFlight = rewriter.create<relalg::InFlightOp>(tripleOp.getLoc(), lowered, availableColumns);
         rewriter.replaceUsesWithIf(tripleOp.getRes(), inFlight.getRes(), [](mlir::OpOperand& use) {
            return isRelAlgOperator(use.getOwner());
         });
      }
      rewriter.replaceOp(tripleOp, lowered);
      return success();
   }
};
class GraphExpansionLowering : public OpConversionPattern<gpm::GraphExpansionOp> {
   NamedGraphMapper& graphs;
   public:
   GraphExpansionLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapper& graphs)
      : OpConversionPattern<gpm::GraphExpansionOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::GraphExpansionOp expansionOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto ctxt = expansionOp.getContext();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      if (mlir::isa<mlir::ArrayAttr>(expansionOp.getP()))
         return expansionOp.emitOpError("predicate lists not yet supported");
      if (expansionOp.getAnchor() == gpm::TripleSlot::predicate) 
         return expansionOp.emitOpError("predicate anchors for graph expansion not yet supported");
      auto boundCols = expansionOp.getUsedColumns();
      boundCols.remove(expansionOp.getCreatedColumns());
      for (auto* column : boundCols) {
         if (mlir::isa<db::NullableType>(column->type)) {
            return expansionOp.emitOpError() << "nullable bindings not yet supported";
         }
      }
      TripleEmitter emitter(rewriter, graphs, expansionOp);
      for (auto* column : boundCols) {
         emitter.bind(columnManager.createRef(column));
      }
      mlir::Value lowered = emitter.lower(adaptor.getRel(), expansionOp.getAnchor());
      rewriter.replaceOp(expansionOp, lowered);
      return success();
   }
};

static mlir::Value refreshStaleRefOperand(mlir::Value operand, ConversionPatternRewriter& rewriter) {
   auto getColOp = mlir::dyn_cast_or_null<tuples::GetColumnOp>(operand.getDefiningOp());
   if (!getColOp) return operand;
   mlir::Type liveType = getColOp.getAttr().getColumn().type;
   if (liveType == operand.getType()) return operand;
   return rewriter.create<tuples::GetColumnOp>(getColOp.getLoc(), liveType, getColOp.getAttr(), getColOp.getTuple());
}
class GpmBindingsCompatibleLowering : public OpConversionPattern<gpm::BindingsCompatibleOp> {
   public:
   using OpConversionPattern<gpm::BindingsCompatibleOp>::OpConversionPattern;
   LogicalResult matchAndRewrite(gpm::BindingsCompatibleOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto* ctxt = rewriter.getContext();
      auto loc = op.getLoc();
      mlir::Value lhs = refreshStaleRefOperand(adaptor.getLhs(), rewriter);
      mlir::Value rhs = refreshStaleRefOperand(adaptor.getRhs(), rewriter);
      mlir::Value anyNull;
      mlir::Value lhsRaw = lhs;
      mlir::Value rhsRaw = rhs;
      if (mlir::isa<db::NullableType>(lhs.getType())) {
         anyNull = rewriter.create<db::IsNullOp>(loc, lhs);
         lhsRaw = rewriter.create<db::NullableGetVal>(loc, lhs);
      }
      if (mlir::isa<db::NullableType>(rhs.getType())) {
         mlir::Value isNull = rewriter.create<db::IsNullOp>(loc, rhs);
         anyNull = anyNull ? rewriter.create<mlir::arith::OrIOp>(loc, anyNull, isNull).getResult() : isNull;
         rhsRaw = rewriter.create<db::NullableGetVal>(loc, rhs);
      }
      auto cmpType = db::NullableType::get(ctxt, rewriter.getI1Type());
      if (!anyNull) {
         auto cmp = rewriter.create<variant::CmpOp>(loc, cmpType, variant::VariantCmpPredicate::eq, lhsRaw, rhsRaw);
         rewriter.replaceOpWithNewOp<db::DeriveTruth>(op, rewriter.getI1Type(), cmp);
         return success();
      }
      auto ifOp = rewriter.create<mlir::scf::IfOp>(
         loc, anyNull,
         [&](mlir::OpBuilder& b, mlir::Location l) {
            b.create<mlir::scf::YieldOp>(l, b.create<mlir::arith::ConstantIntOp>(l, 1, 1).getResult());
         },
         [&](mlir::OpBuilder& b, mlir::Location l) {
            auto cmp = b.create<variant::CmpOp>(l, cmpType, variant::VariantCmpPredicate::eq, lhsRaw, rhsRaw);
            b.create<mlir::scf::YieldOp>(l, b.create<db::DeriveTruth>(l, b.getI1Type(), cmp).getResult());
         });
      rewriter.replaceOp(op, ifOp.getResult(0));
      return success();
   }
};
class GetBindingOpLowering : public OpConversionPattern<gpm::GetBindingOp> {
   public:
   using OpConversionPattern<gpm::GetBindingOp>::OpConversionPattern;
   LogicalResult matchAndRewrite(gpm::GetBindingOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      mlir::Value binding = refreshStaleRefOperand(adaptor.getVar(), rewriter);
      if (mlir::isa<db::NullableType>(binding.getType())) {
         auto loc = op.getLoc();
         mlir::Value isNull = rewriter.create<db::IsNullOp>(loc, binding);
         mlir::Value rawBinding = rewriter.create<db::NullableGetVal>(loc, binding);
         binding = rewriter.create<variant::UnspecifiedIfOp>(loc, rawBinding.getType(), isNull, rawBinding);
      }
      rewriter.replaceOp(op, binding);
      return success();
   }
};
static void refreshOuterJoinMappingTypes(relalg::OuterJoinOp outerJoinOp) {
   for (mlir::Attribute attr : outerJoinOp.getMapping()) {
      auto defAttr = mlir::cast<tuples::ColumnDefAttr>(attr);
      auto fromExisting = mlir::cast<mlir::ArrayAttr>(defAttr.getFromExisting());
      auto sourceRef = mlir::cast<tuples::ColumnRefAttr>(fromExisting[0]);
      mlir::Type innerType = sourceRef.getColumn().type;
      mlir::Type newType = mlir::isa<db::NullableType>(innerType) ? innerType : db::NullableType::get(outerJoinOp.getContext(), innerType);
      defAttr.getColumn().type = newType;
   }
}
static void refreshUnionMappingTypes(relalg::UnionOp unionOp) {
   for (mlir::Attribute attr : unionOp.getMapping()) {
      auto defAttr = mlir::cast<tuples::ColumnDefAttr>(attr);
      auto fromExisting = mlir::cast<mlir::ArrayAttr>(defAttr.getFromExisting());
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
      if (!mergedType) continue;
      defAttr.getColumn().type = nullable ? db::NullableType::get(unionOp.getContext(), mergedType) : mergedType;
   }
}
static void refreshNullableTypes(ModuleOp module) {
   module.walk([&](mlir::Operation* op) {
      if (auto outerJoinOp = mlir::dyn_cast<relalg::OuterJoinOp>(op)) {
         refreshOuterJoinMappingTypes(outerJoinOp);
      } 
      else if (auto unionOp = mlir::dyn_cast<relalg::UnionOp>(op)) {
         refreshUnionMappingTypes(unionOp);
      }
   });
}


static void addCommonLegalDialects(ConversionTarget& target) {
   target.addLegalDialect<gpu::GPUDialect>();
   target.addLegalDialect<async::AsyncDialect>();
   target.addLegalOp<ModuleOp>();
   target.addLegalOp<UnrealizedConversionCastOp>();
   target.addLegalDialect<subop::SubOperatorDialect>();
   target.addLegalDialect<gsubop::GraphSubOpDialect>();
   target.addLegalDialect<db::DBDialect>();
   target.addLegalDialect<lingodb::compiler::dialect::arrow::ArrowDialect>();
   target.addLegalDialect<relalg::RelAlgDialect>();
   target.addLegalDialect<tuples::TupleStreamDialect>();
   target.addLegalDialect<func::FuncDialect>();
   target.addLegalDialect<memref::MemRefDialect>();
   target.addLegalDialect<arith::ArithDialect>();
   target.addLegalDialect<cf::ControlFlowDialect>();
   target.addLegalDialect<scf::SCFDialect>();
   target.addLegalDialect<util::UtilDialect>();
   target.addLegalDialect<variant::VariantDialect>();
}

void GPMToSubOpLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);

   TypeConverter typeConverter;
   typeConverter.addConversion([](tuples::TupleStreamType t) { return t; });
   typeConverter.addConversion([](mlir::Type t) { return t; });
   auto* ctxt = &getContext();
   ctxt->loadDialect<gsubop::GraphSubOpDialect>();
   ctxt->loadDialect<variant::VariantDialect>();

   NamedGraphMapper graphs;
   ExternalGraphMapper externalGraphs;

   ConversionTarget target(getContext());
   addCommonLegalDialects(target);
   target.addLegalDialect<gpm::GPMDialect>();
   target.addIllegalOp<gpm::NamedGraphOp, gpm::TriplePatternOp, gpm::GraphExpansionOp>();

   RewritePatternSet patterns(ctxt);
   patterns.insert<NamedGraphLowering>(typeConverter, ctxt, graphs, externalGraphs);
   patterns.insert<TriplePatternLowering>(typeConverter, ctxt, graphs);
   patterns.insert<GraphExpansionLowering>(typeConverter, ctxt, graphs);

   if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
      return;
   }

   refreshNullableTypes(module);
}
static void expandMergedColumns(ModuleOp module) {
   auto* ctxt = module.getContext();
   auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   module.walk([&](relalg::OuterJoinOp join) {
      auto mergedCols = join->getAttrOfType<mlir::ArrayAttr>(gpm::kMergedColumnsAttr);
      if (!mergedCols) return;
      join->removeAttr(gpm::kMergedColumnsAttr);
      auto loc = join.getLoc();
      OpBuilder builder(join);
      builder.setInsertionPointAfter(join);
      auto* block = new Block;
      mlir::Value tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
      {
         OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         llvm::SmallVector<mlir::Value> results;
         for (auto attr : mergedCols) {
            auto mergedDef = mlir::cast<tuples::ColumnDefAttr>(attr);
            auto sources = mlir::cast<mlir::ArrayAttr>(mergedDef.getFromExisting());
            auto leftRef = mlir::cast<tuples::ColumnRefAttr>(sources[0]);
            auto rightRef = mlir::cast<tuples::ColumnRefAttr>(sources[1]);
            mlir::Value left = builder.create<tuples::GetColumnOp>(loc, leftRef.getColumn().type, leftRef, tuple);
            mergedDef.getColumn().type = left.getType();
            if (!mlir::isa<db::NullableType>(left.getType())) {
               results.push_back(left);
               continue;
            }
            mlir::Value right = builder.create<tuples::GetColumnOp>(loc, rightRef.getColumn().type, rightRef, tuple);
            if (right.getType() != left.getType()) right = builder.create<db::AsNullableOp>(loc, left.getType(), right);
            mlir::Value leftIsNull = builder.create<db::IsNullOp>(loc, left);
            auto ifOp = builder.create<scf::IfOp>(
               loc, leftIsNull,
               [&](OpBuilder& b, Location l) { b.create<scf::YieldOp>(l, right); },
               [&](OpBuilder& b, Location l) { b.create<scf::YieldOp>(l, left); });
            results.push_back(ifOp.getResult(0));
         }
         builder.create<tuples::ReturnOp>(loc, results);
      }
      llvm::SmallVector<mlir::Attribute> computedCols;
      for (auto attr : mergedCols) {
         auto mergedDef = mlir::cast<tuples::ColumnDefAttr>(attr);
         computedCols.push_back(columnManager.createDef(&mergedDef.getColumn()));
      }
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), join.getResult(), builder.getArrayAttr(computedCols));
      mapOp.getPredicate().push_back(block);
      join.getResult().replaceAllUsesExcept(mapOp.getResult(), mapOp);
   });
}
void GPMScalarToSubOpLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);

   TypeConverter typeConverter;
   typeConverter.addConversion([](tuples::TupleStreamType t) { return t; });
   typeConverter.addConversion([](mlir::Type t) { return t; });
   auto* ctxt = &getContext();
   ctxt->loadDialect<gsubop::GraphSubOpDialect>();
   ctxt->loadDialect<variant::VariantDialect>();

   ConversionTarget target(getContext());
   addCommonLegalDialects(target);
   target.addIllegalDialect<gpm::GPMDialect>();

   RewritePatternSet patterns(ctxt);
   patterns.insert<GpmBindingsCompatibleLowering>(typeConverter, ctxt);
   patterns.insert<GetBindingOpLowering>(typeConverter, ctxt);

   expandMergedColumns(module);

   if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
      return;
   }
}
} // namespace
std::unique_ptr<mlir::Pass>
gpm::createLowerToSubOpPass() {
   return std::make_unique<GPMToSubOpLoweringPass>();
}
std::unique_ptr<mlir::Pass>
gpm::createLowerGPMScalarsToSubOpPass() {
   return std::make_unique<GPMScalarToSubOpLoweringPass>();
}
void gpm::createLowerGPMToSubOpPipeline(mlir::OpPassManager& pm) {
   pm.addPass(gpm::createLowerToSubOpPass());
   pm.addPass(gpm::createLowerGPMScalarsToSubOpPass());
}
void gpm::registerGPMToSubOpConversionPasses() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gpm::createLowerToSubOpPass();
   });
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return gpm::createLowerGPMScalarsToSubOpPass();
   });
   mlir::PassPipelineRegistration<EmptyPipelineOptions>(
      "lower-gpm-to-subop",
      "",
      gpm::createLowerGPMToSubOpPipeline);
}
