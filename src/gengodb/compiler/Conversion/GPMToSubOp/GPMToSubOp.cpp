#include "gengodb/compiler/Conversion/GPMToSubOp/GPMToSubOpPass.h"

#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
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
using DefMappingCollector = llvm::SmallVector<subop::DefMappingPairT>;
using RefMappingCollector = llvm::SmallVector<subop::RefMappingPairT>;
struct NamedGraphData {
   gsubop::GraphType graphType;
   tuples::Column* nodeSetColumn;
   tuples::Column* edgeSetColumn;
};
using NamedGraphMapping = llvm::DenseMap<mlir::SymbolRefAttr, NamedGraphData>;
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
inline static std::string memberName(std::string name, std::string prefix = "", std::string suffix = "") {
   return (suffix.empty() ? "" : prefix + "_") + name + (suffix.empty() ? "" : "_" + suffix);
}
template<typename GSetType>
inline static GSetType createGraphSetType(MLIRContext* ctxt, std::string group, std::string graph, std::string name, std::string itStrategy = "all") {
   auto itType = gsubop::GraphSetIteratorType::get(ctxt, mlir::ArrayAttr::get(ctxt, {StringAttr::get(ctxt, itStrategy)}));
   auto itMember = createMember(ctxt, memberName(graph, group, name + "_it"), itType);
   return GSetType::get(ctxt, createStateMembersAttr(ctxt, {itMember}));
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
template<typename ColumnAttrT>
inline static std::pair<std::string, std::string> splitGraphRef(ColumnAttrT columnAttr) {
   auto refType = mlir::cast<gpm::GraphReferenceType>(columnAttr.getColumn().type);
   return {columnAttr.getName().getRootReference().str(), refType.getName().str()};
}
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

template <typename BuildIdentFn>
static mlir::Value generateTupleStream(ConversionPatternRewriter& rewriter, mlir::Location loc, tuples::ColumnDefAttr def, BuildIdentFn buildIdent) {
   auto ctxt = rewriter.getContext();
   auto generateOp = rewriter.create<subop::GenerateOp>(loc, std::vector<mlir::Type>{
      tuples::TupleStreamType::get(ctxt), tuples::TupleStreamType::get(ctxt)}, rewriter.getArrayAttr({def}));
   auto* generateBlock = new Block;
   mlir::OpBuilder::InsertionGuard guard(rewriter);
   generateOp.getRegion().push_back(generateBlock);
   rewriter.setInsertionPointToStart(generateBlock);
   mlir::Value identValue = buildIdent(rewriter);
   rewriter.create<subop::GenerateEmitOp>(loc, mlir::ValueRange{identValue});
   rewriter.create<tuples::ReturnOp>(loc);
   return generateOp.getRes();
}

template<typename ColumnAttrT>
static mlir::Value scanNamedGraph(ConversionPatternRewriter& rewriter, mlir::Location loc, ColumnAttrT graphAttr, NamedGraphMapping& graphs, bool uniqueScope = false) {
   auto ctxt = rewriter.getContext();
   auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto graphRef = graphAttr.getName();
   auto [graphGroup, graphName] = splitGraphRef(graphAttr);
   auto graphRefType = mlir::cast<gpm::GraphReferenceType>(graphAttr.getColumn().type);
   gsubop::GraphType graphType;
   tuples::ColumnDefAttr nodeSetDef, edgeSetDef;
   auto graphNameAttr = graphRefType.getName();
   auto it = graphs.find(graphRef);
   if (it != graphs.end()) {
      graphType = it->second.graphType;
      nodeSetDef = columnManager.createDef(it->second.nodeSetColumn);
      edgeSetDef = columnManager.createDef(it->second.edgeSetColumn);
   }
   else {
      auto nodeSetType = createGraphSetType<gsubop::NodeSetType>(ctxt, graphGroup, graphName, "vx");
      auto edgeSetType = createGraphSetType<gsubop::EdgeSetType>(ctxt, graphGroup, graphName, "ex");
      auto nodeSetMember = createMember(ctxt, memberName(graphName, graphGroup, "vx"), nodeSetType);
      auto edgeSetMember = createMember(ctxt, memberName(graphName, graphGroup, "ex"), edgeSetType);
      graphType = gsubop::GraphType::get(ctxt, createStateMembersAttr(ctxt, {nodeSetMember}), createStateMembersAttr(ctxt, {edgeSetMember}));
      nodeSetDef = createDef(columnManager, graphGroup, graphName + "_vx", nodeSetType, uniqueScope);
      edgeSetDef = createDef(columnManager, graphGroup, graphName + "_ex", edgeSetType, uniqueScope);
      graphs.insert({graphRef, NamedGraphData{graphType, &nodeSetDef.getColumn(), &edgeSetDef.getColumn()}});
   }
   mlir::Value externalGraph = rewriter.create<gsubop::GetExternalGraphOp>(loc, graphType, graphNameAttr, graphRefType.getGlobalId());
   return rewriter.create<gsubop::ScanGraphOp>(loc, externalGraph, nodeSetDef, edgeSetDef);
}

class NamedGraphLowering : public OpConversionPattern<gpm::NamedGraphOp> {
   NamedGraphMapping& graphs;
   public:
   NamedGraphLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapping& graphs)
      : OpConversionPattern<gpm::NamedGraphOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::NamedGraphOp namedGraphOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.replaceOp(namedGraphOp, scanNamedGraph(rewriter, namedGraphOp->getLoc(), namedGraphOp.getDef(), graphs, true));
      return success();
   }
};

// class TriplePatternEmitter {
//    ConversionPatternRewriter& rewriter;
//    MLIRContext* ctxt;
//    tuples::ColumnManager& columnManager;
//    subop::MemberManager& memberManager;
//    public:
//    TriplePatternEmitter(ConversionPatternRewriter& rewriter)
//       : rewriter(rewriter), ctxt(rewriter.getContext()), 
//       columnManager(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()), 
//       memberManager(ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager()) {}
//    static bool reusesExistingBinding(mlir::Attribute term, const TripleEmitContext& emitCtxt) {
//       if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
//          return var.hasBinding();
//       if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term))
//          return emitCtxt.localIdents.count(bnode.getLocalId()) > 0;
//       return false;
//    }
//    mlir::Value lowerTriple(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
//       auto& triple = (*triples)[index];
//       bool anchorIsSubject = isBound(triple.s, emitCtxt.localIdents);
//       bool anchorIsObject = !anchorIsSubject && isBound(triple.o, emitCtxt.localIdents);
//       // bool needsRestart = reusesExistingBinding(triple.p, ectxt) ||
//       //    (!anchorIsSubject && reusesExistingBinding(triple.s, ectxt)) ||
//       //    (!anchorIsObject && reusesExistingBinding(triple.o, ectxt));
//       // if (needsRestart) {
//       //    stream = scanNamedGraph(rewriter, loc, triple.graphRef, ectxt.graphs);
//       // }
//       if (anchorIsSubject)
//          stream = lowerSubjectFirst(loc, stream, triples, index, emitCtxt);
//       else if (anchorIsObject)
//          stream = lowerObjectFirst(loc, stream, triples, index, emitCtxt);
//       else
//          stream = lowerPredicateFirst(loc, stream, triples, index, emitCtxt);
//       return stream;
//    }
//    private:
//    mlir::Value resolveAnchorNode(mlir::Location loc, mlir::Value stream, mlir::Attribute term, tuples::ColumnRefAttr graphRefAttr, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef, TripleEmitContext& emitCtxt) const {
//       auto graphRef = graphRefAttr.getName();
//       auto [group, graph] = splitGraphRef(graphRefAttr);
//       if (auto identTermAttr = mlir::dyn_cast_or_null<gpm::IdentifierTermAttr>(term)) {
//          auto& graphData = emitCtxt.graphs[graphRef];
//          auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
//          auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef}));
//          auto* b = new Block();
//          b->addArgument(tuples::TupleType::get(ctxt), loc);
//          auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
//          nestedMapOp.getRegion().push_back(b);
//          {
//             mlir::OpBuilder::InsertionGuard guard(rewriter);
//             rewriter.setInsertionPointToStart(b);
//             auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
//             auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& b) -> mlir::Value {
//                return b.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, identTermAttr.getIdent());
//             });
//             nodeRefType = createNodeRefType(ctxt, group, graph);
//             auto [nodeDef, resolvedRef] = createColumn(nodeRefType, "nodes", "ref");
//             mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({identRef}), nodeDef);
//             rewriter.create<tuples::ReturnOp>(loc, lookup);
//             nodeRef = resolvedRef;
//          }
//          return nestedMapOp.getRes();
//       }
//       if (auto varTermAttr = mlir::dyn_cast_or_null<gpm::VariableTermAttr>(term)) {
//          auto bindingDef = emitCtxt.bindings[varTermAttr.getBindingReference().getName()].def;
//          nodeRef = columnManager.createRef(bindingDef.getColumnPtr().get());
//          nodeRefType = mlir::cast<gsubop::NodeRefType>(bindingDef.getColumn().type);
//          return stream;
//       }
//       if (auto bnodeTermAttr = mlir::dyn_cast_or_null<gpm::BNodeTermAttr>(term)) {
//          auto bindingDef = emitCtxt.localIdents[bnodeTermAttr.getLocalId()].def;
//          nodeRef = columnManager.createRef(bindingDef.getColumnPtr().get());
//          nodeRefType = mlir::cast<gsubop::NodeRefType>(bindingDef.getColumn().type);
//          return stream;
//       }
//       return stream;
//    }
//    mlir::Value scanEdges(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr edgesRef, mlir::Type edgeSetType, const std::string& group, const std::string& graph, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef) const {
//       auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({edgesRef}));
//       auto* b = new Block();
//       b->addArgument(tuples::TupleType::get(ctxt), loc);
//       auto edgeSetArg = b->addArgument(edgeSetType, loc);
//       edgeRefType = createEdgeRefType(ctxt, group, graph);
//       auto [edgeRefColumnDef, resolvedRef] = createColumn(edgeRefType, "edges", "ref");
//       nestedMapOp.getRegion().push_back(b);
//       {
//          mlir::OpBuilder::InsertionGuard guard(rewriter);
//          rewriter.setInsertionPointToStart(b);
//          mlir::Value inner = rewriter.create<gsubop::ScanEdgeSetOp>(loc, edgeSetArg, edgeRefColumnDef);
//          rewriter.create<tuples::ReturnOp>(loc, inner);
//       }
//       edgeRefColumnRef = resolvedRef;
//       return nestedMapOp.getRes();
//    }
//    mlir::Value filterMatchingIdentifiers(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr leftSource, tuples::ColumnRefAttr rightSource, subop::FilterSemantic semantic) const {
//       auto [leftDef, leftRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "get");
//       auto [rightDef, rightRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "get");
//       auto [filterDef, filterRef] = createColumn(rewriter.getI1Type(), "map", "ident");
//       stream = rewriter.create<gsubop::GatherIdentifierOp>(loc, stream, leftSource, leftDef);
//       stream = rewriter.create<gsubop::GatherIdentifierOp>(loc, stream, rightSource, rightDef);
//       auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({filterDef}), rewriter.getArrayAttr({leftRef, rightRef}));
//       Block* mapBlock = new Block;
//       auto left = mapBlock->addArgument(rewriter.getI32Type(), loc);
//       auto right = mapBlock->addArgument(rewriter.getI32Type(), loc);
//       mapOp.getRegion().push_back(mapBlock);
//       {
//          mlir::OpBuilder::InsertionGuard guard(rewriter);
//          rewriter.setInsertionPointToStart(mapBlock);
//          auto leftI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), left).getResult(0);
//          auto rightI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), right).getResult(0);
//          mlir::Value val = rewriter.create<arith::CmpIOp>(loc, rewriter.getI1Type(), mlir::arith::CmpIPredicate::eq, leftI32, rightI32);
//          rewriter.create<tuples::ReturnOp>(loc, val);
//       }
//       stream = mapOp.getResult();
//       return rewriter.create<subop::FilterOp>(loc, stream, semantic, rewriter.getArrayAttr({filterRef}));
//    }
//    mlir::Value lowerSubjectFirst(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
//       auto& triple = (*triples)[index];
//       auto graphRefAttr = triple.graphRef;
//       auto [group, graph] = splitGraphRef(graphRefAttr);
//       gsubop::NodeRefType nodeRefType;
//       tuples::ColumnRefAttr nodeRef;
//       stream = resolveAnchorNode(loc, stream, triple.s, graphRefAttr, nodeRefType, nodeRef, emitCtxt);
//       auto edgeSetType = memberManager.getType(nodeRefType.getOutgoingMembers().getMembers()[0]);
//       auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "outgoing");
//       stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getOutgoingMembers().getMembers()[0], edgesDef}}));
//       gsubop::EdgeRefType edgeRefType;
//       tuples::ColumnRefAttr edgeRefColumnRef;
//       stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
//       stream = lowerPredicate(loc, stream, graphRefAttr, triple.p, edgeRefColumnRef, columnManager, emitCtxt);
//       stream = lowerTerm(loc, stream, triple.o, edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt);
//       return stream;
//    }
//    mlir::Value lowerObjectFirst(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
//       auto& triple = (*triples)[index];
//       auto graphRefAttr = triple.graphRef;
//       auto [group, graph] = splitGraphRef(graphRefAttr);
//       gsubop::NodeRefType nodeRefType;
//       tuples::ColumnRefAttr nodeRef;
//       stream = resolveAnchorNode(loc, stream, triple.o, graphRefAttr, nodeRefType, nodeRef, emitCtxt);
//       auto edgeSetType = memberManager.getType(nodeRefType.getIncomingMembers().getMembers()[0]);
//       auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "incoming");
//       stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getIncomingMembers().getMembers()[0], edgesDef}}));
//       gsubop::EdgeRefType edgeRefType;
//       tuples::ColumnRefAttr edgeRefColumnRef;
//       stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
//       stream = lowerPredicate(loc, stream, graphRefAttr, triple.p, edgeRefColumnRef, columnManager, emitCtxt);
//       stream = lowerTerm(loc, stream, triple.s, edgeRefType.getFromMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt);
//       return stream;
//    }
//    mlir::Value lowerPredicateFirst(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
//       auto& triple = (*triples)[index];
//       auto graphRefAttr = triple.graphRef;
//       auto [group, graph] = splitGraphRef(graphRefAttr);
//       auto& graphData = emitCtxt.graphs[graphRefAttr.getName()];
//       auto edgesRef = columnManager.createRef(graphData.edgeSetColumn);
//       auto edgeSetType = graphData.edgeSetColumn->type;
//       gsubop::EdgeRefType edgeRefType;
//       tuples::ColumnRefAttr edgeRefColumnRef;
//       stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
//       stream = lowerPredicate(loc, stream, graphRefAttr, triple.p, edgeRefColumnRef, columnManager, emitCtxt);
//       stream = lowerTerm(loc, stream, triple.s, edgeRefType.getFromMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt);
//       stream = lowerTerm(loc, stream, triple.o, edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt);
//       return stream;
//    }
//    mlir::Value lowerPredicate(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr graphRefAttr, mlir::Attribute p, tuples::ColumnRefAttr edgeRef, tuples::ColumnManager& columnManager, TripleEmitContext& emitCtxt) const {
//       auto graphRef = graphRefAttr.getName();
//       auto [group, graph] = splitGraphRef(graphRefAttr);
//       if (auto constPred = mlir::dyn_cast<gpm::IdentifierTermAttr>(p)) {
//          auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constPred.getIdent());
//          stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, ident);
//       }
//       else if (auto varPred = mlir::dyn_cast<gpm::VariableTermAttr>(p)) {
//          if (varPred.hasBinding()) {
//             stream = filterMatchingIdentifiers(loc, stream, varPred.getBindingReference(), edgeRef, subop::FilterSemantic::all_true);
//          }
//          else {
//             auto [identColumnDef, identColumnRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "map");
//             stream = rewriter.create<gsubop::GatherIdentifierOp>(loc, stream, edgeRef, identColumnDef);
//             auto nodeRefType = createNodeRefType(ctxt, group, graph);
//             auto nodesRef = columnManager.createRef(emitCtxt.graphs[graphRef].nodeSetColumn);
//             auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef, identColumnRef}));
//             auto* b = new Block();
//             b->addArgument(tuples::TupleType::get(ctxt), loc);
//             auto nodeSetArg = b->addArgument(emitCtxt.graphs[graphRef].nodeSetColumn->type, loc);
//             auto identArg = b->addArgument(gsubop::IdentifierType::get(ctxt), loc);
//             nestedMapOp.getRegion().push_back(b);
//             {
//                mlir::OpBuilder::InsertionGuard guard(rewriter);
//                rewriter.setInsertionPointToStart(b);
//                auto [scanIdentDef, scanIdentRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "scan");
//                mlir::Value inner = generateTupleStream(rewriter, loc, scanIdentDef, [&](mlir::OpBuilder&) -> mlir::Value {
//                   return identArg;
//                });
//                auto ref = varPred.getProducedBinding();
//                auto def = createDef(columnManager, ref.getName().getRootReference().str(), ref.getName().getLeafReference().str(), nodeRefType, false);
//                emitCtxt.bindings[ref.getName()].def = def;
//                inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), def);
//                rewriter.create<tuples::ReturnOp>(loc, inner);
//             }
//             stream = nestedMapOp;
//          }
//       }
//       return stream;
//    }
//    mlir::Value lowerTerm(mlir::Location loc, mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::string graph, TripleEmitContext& emitCtxt) const {
//       auto ctxt = rewriter.getContext();
//       auto& memberManager = ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
//       if (auto constTerm = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
//          auto [def, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "id");
//          auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constTerm.getIdent());
//          stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
//          stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, ident);
//       }
//       else if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
//          if (varTerm.hasBinding()) {
//             auto [nodeRefColumnDef, nodeRefColumnRef] = createColumn(memberManager.getType(nodeMember), "nodes", "ref");
//             stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, nodeRefColumnDef}}));
//             stream = filterMatchingIdentifiers(loc, stream, varTerm.getBindingReference(), nodeRefColumnRef, subop::FilterSemantic::all_true);
//          }
//          else {
//             auto ref = varTerm.getProducedBinding();
//             auto def = createDef(columnManager, ref.getName().getRootReference().str(), ref.getName().getLeafReference().str(), memberManager.getType(nodeMember), false);
//             emitCtxt.bindings[ref.getName()].def = def;
//             stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
//          }
//       }
//       else if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
//          if (emitCtxt.localIdents.count(bnode.getLocalId()) > 0) {
//             auto bindingDef = emitCtxt.localIdents[bnode.getLocalId()].def;
//             auto bindingRef = columnManager.createRef(bindingDef.getColumnPtr().get());
//             auto [nodeRefColumnDef, nodeRefColumnRef] = createColumn(memberManager.getType(nodeMember), "nodes", "ref");
//             stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, nodeRefColumnDef}}));
//             stream = filterMatchingIdentifiers(loc, stream, bindingRef, nodeRefColumnRef, subop::FilterSemantic::all_true);
//          }
//          else {
//             auto def = createDef(columnManager, "bnode", bnode.localId(), memberManager.getType(nodeMember), true);
//             emitCtxt.localIdents[bnode.getLocalId()].def = def;
//             stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
//          }
//       }
//       return stream;
//    }
// };

// class BasicGraphPatternLowering : public OpConversionPattern<gpm::BasicGraphPatternOp> {
//    NamedGraphMapping& graphs;
//    VariableBinding& bindings;
//    public:
//    BasicGraphPatternLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapping& graphs, VariableBinding& bindings)
//       : OpConversionPattern<gpm::BasicGraphPatternOp>(typeConverter, context), graphs(graphs), bindings(bindings) {}
//    LogicalResult matchAndRewrite(gpm::BasicGraphPatternOp basicGraphPatternOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
//       auto& block = basicGraphPatternOp.getPattern().front();
//       TripleList triples = std::make_shared<llvm::SmallVector<TripleData>>();
//       for (auto& op : block.without_terminator()) {
//          auto triple = mlir::cast<gpm::TriplePatternOp>(&op);
//          triples->push_back(TripleData{triple.getGraphRef(), triple.getS(), triple.getP(), triple.getO()});
//       }
//       rewriter.setInsertionPoint(basicGraphPatternOp);
//       mlir::Value stream = adaptor.getRel();
//       TripleEmitContext emitCtxt{graphs, bindings, LocalIdentifierMapping()};
//       TriplePatternEmitter emitter(rewriter);
//       auto loc = basicGraphPatternOp->getLoc();
//       for (size_t i = 0; i < triples->size(); ++i) {
//          stream = emitter.lowerTriple(loc, stream, triples, i, emitCtxt);
//       }
//       rewriter.replaceOp(basicGraphPatternOp, stream);
//       return success();
//    }
// };

void GPMToSubOpLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);

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
   typeConverter.addConversion([](mlir::Type t) { return t; });
   auto* ctxt = &getContext();
   ctxt->loadDialect<gsubop::GraphSubOpDialect>();
   RewritePatternSet patterns(ctxt);

   NamedGraphMapping graphs;

   patterns.insert<NamedGraphLowering>(typeConverter, ctxt, graphs);

   if (failed(applyFullConversion(module, target, std::move(patterns))))
      signalPassFailure();
}
} // namespace
std::unique_ptr<mlir::Pass>
gpm::createLowerToSubOpPass() {
   return std::make_unique<GPMToSubOpLoweringPass>();
}
void gpm::createLowerGPMToSubOpPipeline(mlir::OpPassManager& pm) {
   //pm.addPass(gpm::createCreateRelAlgInFlightsPass());
   pm.addPass(gpm::createLowerToSubOpPass());
   //pm.addPass(gpm::createPrepareRelAlgLoweringPass());
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
