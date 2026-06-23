#include "gengodb/compiler/Conversion/GPMToSubOp/GPMToSubOpPass.h"

#include "lingodb/compiler/Conversion/RelAlgToSubOp/OrderedAttributes.h"
#include "lingodb/compiler/Dialect/Arrow/IR/ArrowDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgDialect.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
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
using DefMappingCollector = llvm::SmallVector<subop::DefMappingPairT>;
using RefMappingCollector = llvm::SmallVector<subop::RefMappingPairT>;
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
inline static std::string memberName(std::string name, std::string prefix = "", std::string suffix = "") {
   return (suffix.empty() ? "" : prefix + "_") + name + (suffix.empty() ? "" : "_" + suffix);
}
template<typename GSetType>
inline static GSetType createGraphSetType(MLIRContext* ctxt, std::string group, std::string graph, std::string name, std::string itStrategy = "all") {
   auto itType = gsubop::GraphSetIteratorType::get(ctxt, mlir::ArrayAttr::get(ctxt, {StringAttr::get(ctxt, itStrategy)}));
   auto itMember = createMember(ctxt, memberName(graph, group, name + "_it"), itType);
   return GSetType::get(ctxt, createStateMembersAttr(ctxt, {itMember}));
}
template<typename GSetType>
inline static GSetType getGraphSetColumnType(tuples::ColumnManager& columnManager, std::string scope, std::string name) {
   auto col = columnManager.get(scope, name).get();
   return mlir::cast<GSetType>(col->type);
}
inline static tuples::ColumnDefAttr createDef(tuples::ColumnManager& columnManager, std::string scope, std::string name, mlir::Type type, bool uniqueScope = true) {
   scope = uniqueScope ? columnManager.getUniqueScope(scope) : scope;
   auto def = columnManager.createDef(scope, name);
   def.getColumn().type = type;
   return def;
}
inline static tuples::ColumnRefAttr createRef(tuples::ColumnManager& columnManager, std::string scope, std::string name) {
   auto col = columnManager.get(scope, name);
   return columnManager.createRef(col.get());
}
static subop::ColumnDefMemberMappingAttr createColumnDefMemberMappingAttr(MLIRContext* context, DefMappingCollector pairs) {
   return subop::ColumnDefMemberMappingAttr::get(context, pairs);
}
// static subop::ColumnRefMemberMappingAttr createColumnRefMemberMappingAttr(MLIRContext* context, RefMappingCollector pairs) {
//    return subop::ColumnRefMemberMappingAttr::get(context, pairs);
// }
inline static gsubop::NodeRefType createNodeRefType(MLIRContext* ctxt, std::string group, std::string graph) {
   auto edgeSetType0 = createGraphSetType<gsubop::EdgeSetType>(ctxt, group, graph, "incoming", "incoming");
   auto edgeSetType1 = createGraphSetType<gsubop::EdgeSetType>(ctxt, group, graph, "outgoing", "outgoing");
   auto nodeId = createMember(ctxt, memberName(graph, group, "node"), mlir::IntegerType::get(ctxt, 32));
   auto incoming = createMember(ctxt, memberName(graph, group, "incoming"), edgeSetType0);
   auto outgoing = createMember(ctxt, memberName(graph, group, "outgoing"), edgeSetType1);
   auto property = createMember(ctxt, memberName(graph, group, "property"), gsubop::PropertyRefType::get(ctxt));
   return gsubop::NodeRefType::get(ctxt, 
      createStateMembersAttr(ctxt, {nodeId}), 
      createStateMembersAttr(ctxt, {incoming}), 
      createStateMembersAttr(ctxt, {outgoing}), 
      createStateMembersAttr(ctxt, {property})
   );
}
inline static gsubop::EdgeRefType createEdgeRefType(MLIRContext* ctxt, std::string group, std::string graph) {
   auto edgeId = createMember(ctxt, memberName(graph, group, "edge"), mlir::IntegerType::get(ctxt, 32));
   auto from = createMember(ctxt, memberName(graph, group, "from"), createNodeRefType(ctxt, group, graph));
   auto to = createMember(ctxt, memberName(graph, group, "to"), createNodeRefType(ctxt, group, graph));
   auto property = createMember(ctxt, memberName(graph, group, "property"), gsubop::PropertyRefType::get(ctxt));
   return gsubop::EdgeRefType::get(ctxt, 
      createStateMembersAttr(ctxt, {edgeId}), 
      createStateMembersAttr(ctxt, {from}), 
      createStateMembersAttr(ctxt, {to}), 
      createStateMembersAttr(ctxt, {property})
   );
}
static std::pair<tuples::ColumnDefAttr, tuples::ColumnRefAttr> createColumn(mlir::Type type, std::string scope, std::string name) {
   auto& columnManager = type.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto def = createDef(columnManager, scope, name, type);
   auto ref = createRef(columnManager, def.getName().getRootReference().str(), def.getName().getLeafReference().str());
   return {def, ref};
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
      auto nodeSetMember = createMember(ctxt, memberName(graphName, graphGroup, "vx"), nodeSetType);
      auto edgeSetMember = createMember(ctxt, memberName(graphName, graphGroup, "ex"), edgeSetType);
      auto graphType = gsubop::GraphType::get(ctxt, createStateMembersAttr(ctxt, {nodeSetMember}), createStateMembersAttr(ctxt, {edgeSetMember}));
      auto nodeSetDef = createDef(columnManager, graphGroup, graphName + "_vx", nodeSetType, false);
      auto edgeSetDef = createDef(columnManager, graphGroup, graphName + "_ex", edgeSetType, false);
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
      // basicGraphPatternOp->getParentOfType<ModuleOp>().dump();
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
      else
         return failure();
      rewriter.replaceOp(triplePatternOp, stream);
      return success();
   }
   private:
   mlir::Value lowerSubjectFirst(ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp op, LocalIdentifierMapping& localIdents, tuples::ColumnManager& columnManager) const {
      auto ctxt = rewriter.getContext();
      auto& memberManager = ctxt->getLoadedDialect<SubOperatorDialect>()->getMemberManager();
      std::string group = op.getGraphRef().getName().getRootReference().str();
      std::string graph = op.getGraphRef().getName().getLeafReference().str();
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      if (auto identTermAttr = mlir::dyn_cast_or_null<gpm::IdentifierTermAttr>(op.getS())) {
         auto nodesRef = createRef(columnManager, group, graph + "_vx");
         auto identMember = createMember(ctxt, "lookupIdent", gsubop::IdentifierType::get(ctxt));
         auto identStateType = SimpleStateType::get(ctxt, createStateMembersAttr(ctxt, {identMember}));
         auto identState = rewriter.create<gsubop::CreateIdentifierStateOp>(loc, identStateType, graph, identTermAttr.getIdent());
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef}));
         auto* b = new Block();
         b->addArgument(tuples::TupleType::get(ctxt), loc);
         auto nodeSetType = getGraphSetColumnType<gsubop::NodeSetType>(columnManager, group, graph + "_vx");
         auto nodeSetArg = b->addArgument(nodeSetType, loc);
         nestedMapOp.getRegion().push_back(b);
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(b);
            auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
            auto scan = rewriter.create<subop::ScanOp>(loc, identState, createColumnDefMemberMappingAttr(rewriter.getContext(), {{identStateType.getMembers().getMembers()[0], identDef}}));
            nodeRefType = createNodeRefType(ctxt, group, graph);
            auto [nodeDef, nodeRef_] = createColumn(nodeRefType, "nodes", "ref");
            mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({identRef}), nodeDef);
            rewriter.create<tuples::ReturnOp>(loc, lookup);
            nodeRef = nodeRef_;
            // nestedMapOp->getParentOfType<ModuleOp>().dump();
         }
         stream = nestedMapOp.getRes();
      }
      else if (auto bnodeTermAttr = mlir::dyn_cast_or_null<gpm::BNodeTermAttr>(op.getS())) {
         assert(false && "BNodes not yet supported");
      }
      auto edgeSetType = memberManager.getType(nodeRefType.getOutgoingMembers().getMembers()[0]);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "outgoing");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeRefType.getOutgoingMembers().getMembers()[0], edgesDef}}));
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({edgesRef}));
      auto* b = new Block();
      b->addArgument(tuples::TupleType::get(ctxt), loc);
      auto edgeSetArg = b->addArgument(edgeSetType, loc);
      nestedMapOp.getRegion().push_back(b);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         auto edgeRefType = createEdgeRefType(ctxt, group, graph);
         auto [edgeRefColumnDef, edgeRefColumnRef] = createColumn(edgeRefType, "edges", "ref");
         mlir::Value inner = rewriter.create<gsubop::ScanEdgeSetOp>(loc, edgeSetArg, edgeRefColumnDef);
         inner = lowerPredicate(rewriter, loc, inner, op.getP(), edgeRefColumnRef, graph, columnManager);
         inner = lowerTerm(rewriter, loc, inner, op.getO(), edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, localIdents, columnManager);
         rewriter.create<tuples::ReturnOp>(loc, inner);
         // nestedMapOp->getParentOfType<ModuleOp>().dump();
      }
      stream = nestedMapOp;
      return stream;
   }
   mlir::Value lowerPredicate(ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value stream, mlir::Attribute p, tuples::ColumnRefAttr edgeRef, std::string graph, tuples::ColumnManager& columnManager) const {
      if (auto constPred = mlir::dyn_cast<gpm::IdentifierTermAttr>(p)) {
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(rewriter.getContext()), graph, constPred.getIdent());
         stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, ident);
      }
      else if (auto varPred = mlir::dyn_cast<gpm::VariableTermAttr>(p)) {
         if (varPred.hasBinding()) {
            auto [leftDef, leftRef] = createColumn(gsubop::IdentifierType::get(rewriter.getContext()), "idents", "get");
            auto [rightDef, rightRef] = createColumn(gsubop::IdentifierType::get(rewriter.getContext()), "idents", "get");
            auto [filterDef, filterRef] = createColumn(rewriter.getI1Type(), "map", "ident");
            stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, varPred.getBindingReference(), leftDef);
            stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, edgeRef, rightDef);
            auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(rewriter.getContext()), stream, rewriter.getArrayAttr({filterDef}), rewriter.getArrayAttr({leftRef, rightRef}));
            Block* mapBlock = new Block;
            auto left = mapBlock->addArgument(rewriter.getI32Type(), loc);
            auto right = mapBlock->addArgument(rewriter.getI32Type(), loc);
            mapOp.getRegion().push_back(mapBlock);
            {
               mlir::OpBuilder::InsertionGuard guard(rewriter);
               rewriter.setInsertionPointToStart(mapBlock);
               auto leftI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), left).getResult(0);
               auto rightI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), right).getResult(0);
               mlir::Value val = rewriter.create<arith::CmpIOp>(loc, rewriter.getI1Type(), mlir::arith::CmpIPredicate::eq, leftI32, rightI32);
               rewriter.create<tuples::ReturnOp>(loc, val);
            }
            stream = mapOp.getResult();
            stream = rewriter.create<subop::FilterOp>(loc, stream, subop::FilterSemantic::none_true, rewriter.getArrayAttr({filterRef}));
         }
      }
      return stream;
   }
   mlir::Value lowerTerm(ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::string graph, LocalIdentifierMapping& localIdents, tuples::ColumnManager& columnManager) const {
      auto& memberManager = rewriter.getContext()->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
      if (auto constTerm = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
         auto [def, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "id");
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(rewriter.getContext()), graph, constTerm.getIdent());
         stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeMember, def}}));
         stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, ident);
      }
      else if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
         if (varTerm.hasBinding()) {
            auto [nodeRefColumnDef, nodeRefColumnRef] = createColumn(memberManager.getType(nodeMember), "nodes", "ref");
            auto [leftDef, leftRef] = createColumn(gsubop::IdentifierType::get(rewriter.getContext()), "idents", "get");
            auto [rightDef, rightRef] = createColumn(gsubop::IdentifierType::get(rewriter.getContext()), "idents", "get");
            auto [filterDef, filterRef] = createColumn(rewriter.getI1Type(), "map", "ident");
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeMember, nodeRefColumnDef}}));
            stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, varTerm.getBindingReference(), leftDef);
            stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, nodeRefColumnRef, rightDef);
            auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(rewriter.getContext()), stream, rewriter.getArrayAttr({filterDef}), rewriter.getArrayAttr({leftRef, rightRef}));
            Block* mapBlock = new Block;
            auto left = mapBlock->addArgument(rewriter.getI32Type(), loc);
            auto right = mapBlock->addArgument(rewriter.getI32Type(), loc);
            mapOp.getRegion().push_back(mapBlock);
            {
               mlir::OpBuilder::InsertionGuard guard(rewriter);
               rewriter.setInsertionPointToStart(mapBlock);
               auto leftI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), left).getResult(0);
               auto rightI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), right).getResult(0);
               mlir::Value val = rewriter.create<arith::CmpIOp>(loc, rewriter.getI1Type(), mlir::arith::CmpIPredicate::eq, leftI32, rightI32);
               rewriter.create<tuples::ReturnOp>(loc, val);
            }
            stream = mapOp.getResult();
            stream = rewriter.create<subop::FilterOp>(loc, stream, subop::FilterSemantic::none_true, rewriter.getArrayAttr({filterRef}));
         }
         else {
            auto ref = varTerm.getProducedBinding().getName();
            auto def = createDef(columnManager, ref.getRootReference().str(), ref.getLeafReference().str(), memberManager.getType(nodeMember), false);
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(rewriter.getContext(), {{nodeMember, def}}));
            stream.getDefiningOp()->getParentOfType<func::FuncOp>().walk([&](relalg::MaterializeOp op){
               auto ctxt = rewriter.getContext();
               if (!mlir::isa<subop::LocalTableType>(op.getResult().getType()))
                  return;
               mlir::OpBuilder::InsertionGuard guard(rewriter);
               rewriter.setInsertionPointAfter(op);
               auto tableType = mlir::cast<subop::LocalTableType>(op.getResult().getType());
               auto stream = op.getRel();
               llvm::SmallVector<mlir::Attribute, 8> newColRefs;
               for (size_t i = 0; i < op.getCols().size(); i++) {
                  auto colRef = mlir::cast<tuples::ColumnRefAttr>(op.getCols()[i]);
                  if (!mlir::isa<gsubop::NodeRefType>(colRef.getColumn().type) 
                     || mlir::isa<gsubop::EdgeRefType>(colRef.getColumn().type)
                     || mlir::isa<gsubop::PropertyRefType>(colRef.getColumn().type)) {
                        newColRefs.push_back(colRef);
                        continue;
                  }
                  auto tableMember = tableType.getMembers().getMembers()[i];
                  auto [newColDef, newColRef] = createColumn(memberManager.getType(tableMember), "vars", "str");
                  stream = rewriter.create<gsubop::GraphRefToStringOp>(loc, stream, colRef, newColDef);
                  newColRefs.push_back(newColRef);
               }
               auto newOp = rewriter.create<relalg::MaterializeOp>(loc, tableType, stream, ArrayAttr::get(ctxt, newColRefs), op.getColumns());
               rewriter.replaceOp(op, newOp);
            });
         }
      }
      else if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
         assert(false && "BNodes not yet supported");
      }
      return stream;
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

   target.addLegalDialect<relalg::RelAlgDialect>();
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
