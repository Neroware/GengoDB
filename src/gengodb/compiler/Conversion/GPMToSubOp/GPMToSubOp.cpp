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
#include "gengodb/compiler/Conversion/GPMToSubOp/GraphPatternHelper.h"

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
using GraphIdentity = std::pair<mlir::StringAttr, mlir::StringAttr>;
using ExternalGraphMapper = llvm::DenseMap<GraphIdentity, NamedGraph>;
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

static mlir::Value scanNamedGraph(ConversionPatternRewriter& rewriter, mlir::Location loc, NamedGraph& namedGraph, const std::string& group, const std::string& graph, bool uniqueScope = true) {
   auto& columnManager = rewriter.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto nodeSetDef = createDef(columnManager, group, graph + "_vx", namedGraph.nodeSetColumn->type, uniqueScope);
   auto edgeSetDef = createDef(columnManager, group, graph + "_ex", namedGraph.edgeSetColumn->type, uniqueScope);
   namedGraph.nodeSetColumn = &nodeSetDef.getColumn();
   namedGraph.edgeSetColumn = &edgeSetDef.getColumn();
   return rewriter.create<gsubop::ScanGraphOp>(loc, namedGraph.externalGraph, nodeSetDef, edgeSetDef);
}
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
      auto nodeSetDef = &createDef(columnManager, graphGroup, graphName + "_vx", nodeSetType).getColumn();
      auto edgeSetDef = &createDef(columnManager, graphGroup, graphName + "_ex", edgeSetType).getColumn();
      idIt = externalGraphs.insert({identity, NamedGraph{graphType, nodeSetDef, edgeSetDef, externalGraph}}).first;
   }
   NamedGraph namedGraph = idIt->second;
   auto scan = scanNamedGraph(rewriter, loc, namedGraph, graphGroup, graphName, uniqueScope);
   graphs.insert({graphRef, namedGraph});
   return scan;
}
static bool isRelAlgOperator(mlir::Operation* op) {
   return op && !mlir::isa<gpm::GPMOperator>(op) && mlir::isa<Operator>(op);
}
static LogicalResult normalized(gpm::TriplePatternOp tripleOp) {
   bool normalized = llvm::all_of(mlir::ArrayRef<mlir::Attribute>{tripleOp.getS(), tripleOp.getP(), tripleOp.getO()}, [](mlir::Attribute term) {
      if (mlir::isa<gpm::IdentifierTermAttr>(term)) return true;
      auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term);
      return varTerm && varTerm.getProducedBinding();
   });
   if (!normalized) {
      return tripleOp.emitOpError("triple pattern is not normalized (expected only identifiers and column defs); run gpm-unnest-patterns first");
   }
   return success();
}
static void replaceTripleOp(gpm::TriplePatternOp tripleOp, mlir::Value lowered, ConversionPatternRewriter& rewriter) {
   bool needsInFlight = llvm::any_of(tripleOp.getRes().getUses(), [](mlir::OpOperand& use) {
      return isRelAlgOperator(use.getOwner());
   });
   if (needsInFlight) {
      relalg::AvailabilityCache availabilityCache;
      auto availableColumns = mlir::cast<Operator>(tripleOp.getOperation()).getAvailableColumns(availabilityCache).asRefArrayAttr(rewriter.getContext());
      auto inFlight = rewriter.create<relalg::InFlightOp>(tripleOp.getLoc(), lowered, availableColumns);
      rewriter.replaceUsesWithIf(tripleOp.getRes(), inFlight.getRes(), [](mlir::OpOperand& use) {
         return isRelAlgOperator(use.getOwner());
      });
   }
   rewriter.replaceOp(tripleOp, lowered);
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
      if (tripleOp->hasAttr("virtual"))
         return failure();
      if (failed(normalized(tripleOp)))
         return failure();
      TripleEmitter emitter(rewriter, graphs, tripleOp);
      emitter.setUseEdgeWeights(tripleOp->hasAttr("truthDegr"));
      replaceTripleOp(tripleOp, emitter.lower(adaptor.getRel()), rewriter);
      return success();
   }
};
class VirtualTriplePatternLowering : public OpConversionPattern<gpm::TriplePatternOp> {
   public:
   VirtualTriplePatternLowering(TypeConverter& typeConverter, MLIRContext* context, int64_t& nextIdent)
      : OpConversionPattern<gpm::TriplePatternOp>(typeConverter, context), nextIdent(nextIdent) {}
   LogicalResult matchAndRewrite(gpm::TriplePatternOp tripleOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      if (!tripleOp->hasAttr("virtual"))
         return failure();
      if (failed(normalized(tripleOp)))
         return failure();
      mlir::Operation* join = findGraphIndexJoin(tripleOp);
      if (!join)
         return tripleOp.emitOpError("virtual triple pattern is not consumed by a graph index join (useGraphIndex)");
      int64_t id = nextIdent++;
      auto idAttr = rewriter.getI64IntegerAttr(id);
      rewriter.modifyOpInPlace(join, [&]() { join->setAttr(kGraphIdxExpId, idAttr); });
      for (const auto* column : tripleOp.getCreatedVariables()) {
         assert(mlir::isa<variant::VariantType>(column->type) && "column not a variant");
         (void) column;
      }
      mlir::Value placeholder = rewriter.create<gsubop::GraphIndexExpandOp>(tripleOp.getLoc(), tuples::TupleStreamType::get(rewriter.getContext()), adaptor.getRel(), asDictAttr(tripleOp), idAttr);
      replaceTripleOp(tripleOp, placeholder, rewriter);
      return success();
   }
   private:
   static mlir::Operation* findGraphIndexJoin(gpm::TriplePatternOp tripleOp) {
      mlir::Value current = tripleOp.getRes();
      while (true) {
         if (!current.hasOneUse()) return nullptr;
         mlir::Operation* user = *current.getUsers().begin();
         if (user->hasAttr("useGraphIndex")) return user;
         if (user->getNumResults() != 1 || !mlir::isa<tuples::TupleStreamType>(user->getResult(0).getType())) return nullptr;
         current = user->getResult(0);
      }
   }
   static mlir::DictionaryAttr asDictAttr(gpm::TriplePatternOp tripleOp) {
      mlir::Builder b(tripleOp.getContext());
      llvm::SmallVector<mlir::Attribute> paramIds;
      for (auto slot : {gpm::TripleSlot::subject, gpm::TripleSlot::predicate, gpm::TripleSlot::object}) {
         auto id = tripleOp.getParamId(slot);
         paramIds.push_back(b.getI64IntegerAttr(id ? static_cast<int64_t>(*id) : -1));
      }
      llvm::SmallVector<mlir::NamedAttribute> entries{b.getNamedAttr("graphRef", tripleOp.getGraphRef()), b.getNamedAttr("s", tripleOp.getS()),
         b.getNamedAttr("p", tripleOp.getP()), b.getNamedAttr("o", tripleOp.getO()), b.getNamedAttr("paramIds", b.getArrayAttr(paramIds))};
      if (auto truthDegr = tripleOp.getTruthDegrAttr()) entries.push_back(b.getNamedAttr("truthDegr", truthDegr));
      return b.getDictionaryAttr(entries);
   }
   private:
   int64_t& nextIdent;
};
class NodeExpansionLowering : public OpConversionPattern<gpm::NodeExpansionOp> {
   NamedGraphMapper& graphs;
   public:
   NodeExpansionLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapper& graphs)
      : OpConversionPattern<gpm::NodeExpansionOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::NodeExpansionOp expansionOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      GraphPatternHelper::AnchorT anchor;
      if (auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(expansionOp.getAnchorTerm())) {
         anchor = ident;
      } else {
         auto anchorRef = mlir::cast<gpm::VariableTermAttr>(expansionOp.getAnchorTerm()).getBindingReference();
         if (mlir::isa<db::NullableType>(anchorRef.getColumn().type))
            return expansionOp.emitOpError() << "nullable anchors not yet supported";
         anchor = anchorRef;
      }
      auto direction = expansionOp.getAnchor() == gpm::ExpansionDirection::outgoing ? GraphPatternHelper::Outgoing : GraphPatternHelper::Incoming;
      auto targetDef = mlir::cast<gpm::VariableTermAttr>(expansionOp.getTarget()).getProducedBinding();
      assert(mlir::isa<variant::VariantType>(targetDef.getColumn().type) && "column not a variant");
      GraphPatternHelper emitter(rewriter, graphs, expansionOp, expansionOp.getGraphRef());
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRef;
      mlir::Value lowered = emitter.emitNodeToEdge(adaptor.getRel(), anchor, direction, edgeRefType, edgeRef, expansionOp.getParamId());
      lowered = emitter.wrapEdge(lowered, edgeRef, targetDef);
      rewriter.replaceOp(expansionOp, lowered);
      return success();
   }
};
class EdgeExpansionLowering : public OpConversionPattern<gpm::EdgeExpansionOp> {
   NamedGraphMapper& graphs;
   public:
   EdgeExpansionLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapper& graphs)
      : OpConversionPattern<gpm::EdgeExpansionOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::EdgeExpansionOp expansionOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto targetDef = mlir::cast<gpm::VariableTermAttr>(expansionOp.getTarget()).getProducedBinding();
      assert(mlir::isa<variant::VariantType>(targetDef.getColumn().type) && "column not a variant");
      GraphPatternHelper emitter(rewriter, graphs, expansionOp, expansionOp.getGraphRef());
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRef;
      mlir::Value lowered;
      if (auto predicate = mlir::dyn_cast<gpm::IdentifierTermAttr>(expansionOp.getAnchorTerm())) {
         lowered = emitter.scanEdges(adaptor.getRel(), edgeRefType, edgeRef);
         lowered = emitter.filterByIdentifier(lowered, edgeRef, predicate, expansionOp.getParamId());
      } else {
         auto anchorRef = mlir::cast<gpm::VariableTermAttr>(expansionOp.getAnchorTerm()).getBindingReference();
         if (mlir::isa<db::NullableType>(anchorRef.getColumn().type))
            return expansionOp.emitOpError() << "nullable anchors not yet supported";
         lowered = emitter.resolveEdge(adaptor.getRel(), anchorRef, edgeRefType, edgeRef);
      }
      auto endpointMembers = expansionOp.getAnchor() == gpm::EdgeEndpoint::from_node ? edgeRefType.getFromMembers() : edgeRefType.getToMembers();
      tuples::ColumnRefAttr rawRef;
      lowered = emitter.emitEdgeToNode(lowered, edgeRef, endpointMembers.getMembers()[0], rawRef);
      lowered = emitter.wrapNode(lowered, rawRef, targetDef);
      rewriter.replaceOp(expansionOp, lowered);
      return success();
   }
};

class GpmBindingsCompatibleLowering : public OpConversionPattern<gpm::BindingsCompatibleOp> {
   public:
   using OpConversionPattern<gpm::BindingsCompatibleOp>::OpConversionPattern;
   LogicalResult matchAndRewrite(gpm::BindingsCompatibleOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto* ctxt = rewriter.getContext();
      auto loc = op.getLoc();
      mlir::Value lhs = adaptor.getLhs();
      mlir::Value rhs = adaptor.getRhs();
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
      mlir::Value binding = adaptor.getVar();
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
class PathLowering : public mlir::OpConversionPattern<gpm::PathOp> {
   NamedGraphMapper& graphs;

   public:
   using OpConversionPattern<gpm::PathOp>::OpConversionPattern;
   PathLowering(mlir::TypeConverter& typeConverter, mlir::MLIRContext* context, NamedGraphMapper& graphs)
      : mlir::OpConversionPattern<gpm::PathOp>(typeConverter, context), graphs(graphs) {}

   LogicalResult matchAndRewrite(gpm::PathOp pathOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto* ctxt = rewriter.getContext();
      auto loc = pathOp.getLoc();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto& memberManager = ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
      auto tupleStreamType = tuples::TupleStreamType::get(ctxt);
      auto i1 = rewriter.getI1Type();
      auto i32 = rewriter.getI32Type();

      const tuples::Column* ends[2] = {nullptr, nullptr};
      gpm::IdentifierTermAttr idents[2];
      for (auto [i, term] : llvm::enumerate(std::array<mlir::Attribute, 2>{pathOp.getS(), pathOp.getO()})) {
         if (auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
            idents[i] = ident;
            continue;
         }
         auto varTerm = mlir::cast<gpm::VariableTermAttr>(term);
         if (!varTerm.getProducedBinding())
            return pathOp.emitOpError("path is not normalized, run --gpm-unnest-patterns first");
         ends[i] = &varTerm.getProducedBinding().getColumn();
         if (!mlir::isa<variant::VariantType>(ends[i]->type))
            return pathOp.emitOpError("path end columns must be variants, run --gpm-unnest-patterns first");
      }
      auto graphIt = graphs.find(pathOp.getGraphRef().getName());
      if (graphIt == graphs.end())
         return pathOp.emitOpError("the path's named graph must be lowered before the path");
      const NamedGraph inputGraph = graphIt->second;

      bool forward = idents[0] || !idents[1];
      gpm::IdentifierTermAttr anchorIdent = forward ? idents[0] : idents[1];
      gpm::IdentifierTermAttr otherIdent = forward ? idents[1] : gpm::IdentifierTermAttr();
      auto anchorParam = pathOp.getParamId(forward ? gpm::TripleSlot::subject : gpm::TripleSlot::object);
      auto otherParam = pathOp.getParamId(gpm::TripleSlot::object);
      bool sameVariable = ends[0] && ends[0] == ends[1];
      bool zeroLength = pathOp.getMinLength() == 0;
      auto predicates = llvm::to_vector(pathOp.getPredicates().getValue());

      NamedGraphMapper localGraphs;
      localGraphs[pathOp.getGraphRef().getName()] = inputGraph;
      PathPatternEmitter emitter(rewriter, localGraphs, pathOp, pathOp.getGraphRef());
      std::string group, graph;
      std::tie(group, graph) = emitter.getGraphName();

      auto addAnchor = [&](auto fn) {
         tuples::ColumnRefAttr idRef;
         mlir::Value stream = fn(scanNamedGraph(rewriter, loc, emitter.getGraph(), group, graph), idRef);
         emitter.addAnchor(stream, idRef);
      };
      auto filterByIdentifiers = [&](mlir::Value stream, tuples::ColumnRefAttr edgeRef, llvm::ArrayRef<mlir::Attribute> predicates) -> mlir::Value {
         llvm::SmallVector<mlir::Value> idents;
         for (auto predicate : predicates) {
            idents.push_back(rewriter.create<gsubop::CreateIdentifierOp>(
               loc, gsubop::IdentifierType::get(ctxt), graph, mlir::cast<gpm::IdentifierTermAttr>(predicate).getIdent()));
         }
         return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, idents);
      };
      auto edgeEndpointId = [&](mlir::Value stream, bool fromSource, llvm::ArrayRef<mlir::Attribute> predicates, tuples::ColumnRefAttr& idRef) {
         gsubop::EdgeRefType edgeRefType;
         tuples::ColumnRefAttr edgeRef;
         GraphHelper helper(rewriter, loc);
         stream = emitter.scanEdges(stream, edgeRefType, edgeRef);
         if (!predicates.empty()) stream = filterByIdentifiers(stream, edgeRef, predicates);
         auto endpointMember = fromSource ? edgeRefType.getFromMembers().getMembers()[0] : edgeRefType.getToMembers().getMembers()[0];
         tuples::ColumnRefAttr rawRef;
         stream = emitter.emitEdgeToNode(stream, edgeRef, endpointMember, rawRef);
         return helper.nodeId(stream, rawRef, mlir::cast<gsubop::NodeRefType>(memberManager.getType(endpointMember)), idRef);
      };

      if (anchorIdent) {
         addAnchor([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { 
            return emitter.nodeId(graphStream, anchorIdent, idRef, anchorParam);
         });
      } 
      else if (zeroLength) {
         addAnchor([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { 
            return edgeEndpointId(graphStream, true, {}, idRef);
         });
         addAnchor([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { 
            return edgeEndpointId(graphStream, false, {}, idRef);
         });
      } 
      else {
         addAnchor([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { 
            return edgeEndpointId(graphStream, false, predicates, idRef);
         });
      }
      mlir::Value frontier = emitter.emitFrontier();

      return failure();
   }
};

static void lowerMergedColumns(mlir::Operation* join) {
   auto mergedCols = join->getAttrOfType<mlir::ArrayAttr>(gpm::kMergedColumnsAttr);
   if (!mergedCols) return;
   auto* ctxt = join->getContext();
   auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto loc = join->getLoc();
   join->removeAttr(gpm::kMergedColumnsAttr);
   auto* block = new Block;
   mlir::Value tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
   OpBuilder builder(ctxt);
   builder.setInsertionPointToStart(block);
   llvm::SmallVector<mlir::Value> results;
   for (auto attr : mergedCols) {
      auto mergedDef = mlir::cast<tuples::ColumnDefAttr>(attr);
      auto sources = mlir::cast<mlir::ArrayAttr>(mergedDef.getFromExisting());
      auto leftRef = mlir::cast<tuples::ColumnRefAttr>(sources[0]);
      auto rightRef = mlir::cast<tuples::ColumnRefAttr>(sources[1]);
      auto mergedType = mergedDef.getColumn().type;
      mlir::Value left = builder.create<tuples::GetColumnOp>(loc, leftRef.getColumn().type, leftRef, tuple);
      if (!mlir::isa<db::NullableType>(left.getType())) {
         if (left.getType() != mergedType) left = builder.create<db::AsNullableOp>(loc, mergedType, left);
         results.push_back(left);
         continue;
      }
      assert(left.getType() == mergedType && "merged column has the type of its left source");
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
   llvm::SmallVector<mlir::Attribute> computedCols;
   for (auto attr : mergedCols) {
      computedCols.push_back(columnManager.createDef(&mlir::cast<tuples::ColumnDefAttr>(attr).getColumn()));
   }
   builder.setInsertionPointAfter(join);
   mlir::Value joinResult = join->getResult(0);
   auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), joinResult, builder.getArrayAttr(computedCols));
   mapOp.getPredicate().push_back(block);
   joinResult.replaceAllUsesExcept(mapOp.getResult(), mapOp);
}

static LogicalResult verifyResolvedColumnTypes(ModuleOp module) {
   auto isPlaceholder = [](mlir::Type type) {
      if (auto nullableType = mlir::dyn_cast_or_null<db::NullableType>(type)) type = nullableType.getType();
      return mlir::isa_and_nonnull<gpm::VariableBindingType>(type);
   };
   auto result = module.walk([&](mlir::Operation* op) {
      mlir::WalkResult status = mlir::WalkResult::advance();
      op->getAttrDictionary().walk([&](tuples::ColumnDefAttr def) {
         if (status.wasInterrupted() || !isPlaceholder(def.getColumn().type)) return;
         op->emitOpError() << "column " << def.getName() << " still has type !gpm.variable_binding; run gpm-unnest-patterns first";
         status = mlir::WalkResult::interrupt();
      });
      if (status.wasInterrupted()) return status;
      if (auto getColumnOp = mlir::dyn_cast<tuples::GetColumnOp>(op)) {
         bool readByGpmOp = llvm::any_of(op->getUsers(), [](mlir::Operation* user) { return mlir::isa<gpm::GPMDialect>(user->getDialect()); });
         if (readByGpmOp && getColumnOp.getResult().getType() != getColumnOp.getAttr().getColumn().type) {
            op->emitOpError() << "reads column " << getColumnOp.getAttr().getName() << " with a stale type (column: " << getColumnOp.getAttr().getColumn().type << ")";
            return mlir::WalkResult::interrupt();
         }
      }
      return status;
   });
   return failure(result.wasInterrupted());
}

void GPMToSubOpLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);
   if (failed(verifyResolvedColumnTypes(module))) {
      signalPassFailure();
      return;
   }

   llvm::SmallVector<mlir::Operation*> mergeJoins;
   module.walk([&](mlir::Operation* op) {
      if (mlir::isa<relalg::OuterJoinOp, relalg::InnerJoinOp>(op) && op->hasAttr(gpm::kMergedColumnsAttr)) 
         mergeJoins.push_back(op);
   });
   for (auto* join : mergeJoins) lowerMergedColumns(join);

   TypeConverter typeConverter;
   typeConverter.addConversion([](tuples::TupleStreamType t) { return t; });
   typeConverter.addConversion([](mlir::Type t) { return t; });
   auto* ctxt = &getContext();
   ctxt->loadDialect<gsubop::GraphSubOpDialect>();
   ctxt->loadDialect<variant::VariantDialect>();

   NamedGraphMapper graphs;
   ExternalGraphMapper externalGraphs;
   int64_t graphIdxExpandIds = 0;

   ConversionTarget target(getContext());
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
   target.addIllegalDialect<gpm::GPMDialect>();

   RewritePatternSet patterns(ctxt);
   patterns.insert<NamedGraphLowering>(typeConverter, ctxt, graphs, externalGraphs);
   patterns.insert<TriplePatternLowering>(typeConverter, ctxt, graphs);
   patterns.insert<VirtualTriplePatternLowering>(typeConverter, ctxt, graphIdxExpandIds);
   patterns.insert<NodeExpansionLowering>(typeConverter, ctxt, graphs);
   patterns.insert<EdgeExpansionLowering>(typeConverter, ctxt, graphs);
   patterns.insert<GpmBindingsCompatibleLowering>(typeConverter, ctxt);
   patterns.insert<GetBindingOpLowering>(typeConverter, ctxt);

   if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
   }
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
