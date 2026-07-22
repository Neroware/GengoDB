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

// The algorithm we use to lower triples differentiates terms into four distinct categories,
// which change the behavior of the TriplePatternOp lowering pattern.
enum class TermClass {
   // a constant identifier (e.g. IRI)
   CONSTANT, 
   // first occurrence of a variable/bnode that requires a new binding.
   FRESH, 
   // bound variable/bnode reused across triples.
   EXTERNAL_CANDIDATE, 
   // bound variable/bnode reused in the same triple patten.
   SAME_TRIPLE_REUSE,
};
struct ClassifiedTerm {
   TermClass cls = TermClass::FRESH;
   gpm::IdentifierTermAttr constant;
   std::string targetScope, targetName;
   tuples::ColumnRefAttr existingRef;
};
class TripleEmitter {
   ConversionPatternRewriter& rewriter;
   MLIRContext* ctxt;
   tuples::ColumnManager& columnManager;
   subop::MemberManager& memberManager;
   NamedGraphMapping& graphs;
   mlir::Location loc;
   tuples::ColumnRefAttr graphRefAttr;
   mlir::SymbolRefAttr graphSym;
   std::string group, graph;
   mlir::Attribute sTerm, pTerm, oTerm;
   mlir::DictionaryAttr bindingsAttr, bnodeScopeAttr;
   llvm::DenseMap<const tuples::Column*, tuples::ColumnRefAttr> localTerms;
   public:
   TripleEmitter(ConversionPatternRewriter& rewriter, NamedGraphMapping& graphs, gpm::TriplePatternOp tripleOp)
      : rewriter(rewriter), ctxt(rewriter.getContext()),
      columnManager(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()),
      memberManager(ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager()),
      graphs(graphs), loc(tripleOp->getLoc()), graphRefAttr(tripleOp.getGraphRef()),
      graphSym(graphRefAttr.getName()), sTerm(tripleOp.getS()), pTerm(tripleOp.getP()), oTerm(tripleOp.getO()),
      bindingsAttr(tripleOp->getAttrOfType<mlir::DictionaryAttr>("bindings")),
      bnodeScopeAttr(tripleOp->getAttrOfType<mlir::DictionaryAttr>("bnodeScope")) {
      std::tie(group, graph) = splitGraphRef(graphRefAttr);
      assert(graphs.count(graphSym) && "graph must already be lowered");
   }
   mlir::Value lower(mlir::Value stream) {
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRef;
      bool anchorSubject = mlir::isa<gpm::IdentifierTermAttr>(sTerm);
      bool anchorObject = !anchorSubject && mlir::isa<gpm::IdentifierTermAttr>(oTerm);
      if (anchorSubject) {
         stream = scanFromConstantAnchor(stream, mlir::cast<gpm::IdentifierTermAttr>(sTerm), EdgeDirection::Outgoing, edgeRefType, edgeRef);
         stream = emitPredicate(stream, edgeRef);
         stream = emitTerm(stream, oTerm, "o", edgeRefType.getToMembers().getMembers()[0], edgeRef);
      }
      else if (anchorObject) {
         stream = scanFromConstantAnchor(stream, mlir::cast<gpm::IdentifierTermAttr>(oTerm), EdgeDirection::Incoming, edgeRefType, edgeRef);
         stream = emitTerm(stream, sTerm, "s", edgeRefType.getFromMembers().getMembers()[0], edgeRef);
         stream = emitPredicate(stream, edgeRef);
      }
      else {
         auto& graphData = graphs[graphSym];
         auto edgesRef = columnManager.createRef(graphData.edgeSetColumn);
         auto edgeSetType = graphData.edgeSetColumn->type;
         stream = scanEdges(stream, edgesRef, edgeSetType, edgeRefType, edgeRef);
         stream = emitTerm(stream, sTerm, "s", edgeRefType.getFromMembers().getMembers()[0], edgeRef);
         stream = emitPredicate(stream, edgeRef);
         stream = emitTerm(stream, oTerm, "o", edgeRefType.getToMembers().getMembers()[0], edgeRef);
      }
      return stream;
   }
   private:
   enum EdgeDirection { Incoming, Outgoing };
   ClassifiedTerm classify(mlir::StringRef pos, mlir::Attribute term) {
      if (auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
         ClassifiedTerm ct;
         ct.cls = TermClass::CONSTANT;
         ct.constant = ident;
         return ct;
      }
      if (bindingsAttr) {
         if (auto entry = bindingsAttr.get(pos)) {
            auto name = mlir::cast<tuples::ColumnDefAttr>(entry).getName();
            ClassifiedTerm ct;
            ct.cls = TermClass::EXTERNAL_CANDIDATE;
            ct.targetScope = name.getRootReference().str();
            ct.targetName = name.getLeafReference().str();
            return ct;
         }
      }
      if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
         if (!var.hasBinding()) {
            auto name = var.getProducedBinding().getName();
            ClassifiedTerm ct;
            ct.cls = TermClass::FRESH;
            ct.targetScope = name.getRootReference().str();
            ct.targetName = name.getLeafReference().str();
            return ct;
         }
         auto bindingRef = var.getBindingReference();
         auto it = localTerms.find(&bindingRef.getColumn());
         if (it != localTerms.end()) {
            ClassifiedTerm ct;
            ct.cls = TermClass::SAME_TRIPLE_REUSE;
            ct.existingRef = it->second;
            return ct;
         }
         auto name = bindingRef.getName();
         ClassifiedTerm ct;
         ct.cls = TermClass::FRESH;
         ct.targetScope = name.getRootReference().str();
         ct.targetName = name.getLeafReference().str();
         return ct;
      }
      auto bnode = mlir::cast<gpm::BNodeTermAttr>(term);
      tuples::ColumnRefAttr canonicalRef;
      if (bnodeScopeAttr) {
         if (auto entry = bnodeScopeAttr.get(bnode.getLocalId().getValue()))
            canonicalRef = mlir::cast<tuples::ColumnRefAttr>(entry);
      }
      assert(canonicalRef && "blank node term without a bnodeScope entry");
      auto it = localTerms.find(&canonicalRef.getColumn());
      if (it != localTerms.end()) {
         ClassifiedTerm ct;
         ct.cls = TermClass::SAME_TRIPLE_REUSE;
         ct.existingRef = it->second;
         return ct;
      }
      auto name = canonicalRef.getName();
      ClassifiedTerm ct;
      ct.cls = TermClass::FRESH;
      ct.targetScope = name.getRootReference().str();
      ct.targetName = name.getLeafReference().str();
      return ct;
   }
   tuples::ColumnDefAttr resolveTargetDef(const ClassifiedTerm& term, mlir::Type type) {
      if (term.cls == TermClass::SAME_TRIPLE_REUSE) {
         auto [def, ref] = createColumn(type, "nodes", "tmp");
         (void) ref;
         return def;
      }
      return createDef(columnManager, term.targetScope, term.targetName, type, false);
   }
   mlir::Value finishClassifiedTerm(mlir::Value stream, const ClassifiedTerm& term, tuples::ColumnDefAttr targetDef) {
      auto ref = columnManager.createRef(targetDef.getColumnPtr().get());
      if (term.cls == TermClass::SAME_TRIPLE_REUSE) {
         return filterByIdentifier(stream, term.existingRef, ref);
      }
      localTerms[&targetDef.getColumn()] = ref;
      return stream;
   }
   mlir::Value filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr left, tuples::ColumnRefAttr right) {
      // TODO Get rid of GatherIdentifierOp
      auto [leftIdDef, leftIdRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "self");
      auto [rightIdDef, rightIdRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "self");
      stream = rewriter.create<gsubop::GatherIdentifierOp>(loc, stream, left, leftIdDef);
      stream = rewriter.create<gsubop::GatherIdentifierOp>(loc, stream, right, rightIdDef);
      auto [filterDef, filterRef] = createColumn(rewriter.getI1Type(), "map", "selfeq");
      auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({filterDef}), rewriter.getArrayAttr({leftIdRef, rightIdRef}));
      auto* b = new Block();
      auto lArg = b->addArgument(rewriter.getI32Type(), loc);
      auto rArg = b->addArgument(rewriter.getI32Type(), loc);
      mapOp.getRegion().push_back(b);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         auto leftI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), lArg).getResult(0);
         auto rightI32 = rewriter.create<UnrealizedConversionCastOp>(loc, rewriter.getI32Type(), rArg).getResult(0);
         auto eq = rewriter.create<arith::CmpIOp>(loc, rewriter.getI1Type(), arith::CmpIPredicate::eq, leftI32, rightI32);
         rewriter.create<tuples::ReturnOp>(loc, eq.getResult());
      }
      stream = mapOp.getResult();
      return rewriter.create<subop::FilterOp>(loc, stream, subop::FilterSemantic::all_true, rewriter.getArrayAttr({filterRef}));
   }
   mlir::Value scanFromConstantAnchor(mlir::Value stream, gpm::IdentifierTermAttr ident, EdgeDirection direction, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef) {
      auto& graphData = graphs[graphSym];
      auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef}));
      auto* b = new Block();
      b->addArgument(tuples::TupleType::get(ctxt), loc);
      auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
      nestedMapOp.getRegion().push_back(b);
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
         auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& bldr) -> mlir::Value {
            return bldr.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, ident.getIdent());
         });
         nodeRefType = createNodeRefType(ctxt, group, graph);
         auto [nodeDef, resolvedRef] = createColumn(nodeRefType, "nodes", "ref");
         mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({identRef}), nodeDef);
         rewriter.create<tuples::ReturnOp>(loc, lookup);
         nodeRef = resolvedRef;
      }
      stream = nestedMapOp.getRes();
      auto edgeMember = direction == EdgeDirection::Outgoing ? nodeRefType.getOutgoingMembers().getMembers()[0] : nodeRefType.getIncomingMembers().getMembers()[0];
      auto edgeSetType = memberManager.getType(edgeMember);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", direction == EdgeDirection::Outgoing ? "outgoing" : "incoming");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{edgeMember, edgesDef}}));
      return scanEdges(stream, edgesRef, edgeSetType, edgeRefType, edgeRef);
   }
   mlir::Value scanEdges(mlir::Value stream, tuples::ColumnRefAttr edgesRef, mlir::Type edgeSetType, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef) {
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({edgesRef}));
      auto* b = new Block();
      b->addArgument(tuples::TupleType::get(ctxt), loc);
      auto edgeSetArg = b->addArgument(edgeSetType, loc);
      edgeRefType = createEdgeRefType(ctxt, group, graph);
      auto [edgeRefColumnDef, resolvedRef] = createColumn(edgeRefType, "edges", "ref");
      nestedMapOp.getRegion().push_back(b);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         mlir::Value inner = rewriter.create<gsubop::ScanEdgeSetOp>(loc, edgeSetArg, edgeRefColumnDef);
         rewriter.create<tuples::ReturnOp>(loc, inner);
      }
      edgeRefColumnRef = resolvedRef;
      return nestedMapOp.getRes();
   }
   mlir::Value emitPredicate(mlir::Value stream, tuples::ColumnRefAttr edgeRef) {
      if (auto constPred = mlir::dyn_cast<gpm::IdentifierTermAttr>(pTerm)) {
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constPred.getIdent());
         return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, ident);
      }
      auto ct = classify("p", pTerm);
      auto [identColumnDef, identColumnRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "map");
      stream = rewriter.create<gsubop::GatherIdentifierOp>(loc, stream, edgeRef, identColumnDef);
      auto nodeRefType = createNodeRefType(ctxt, group, graph);
      auto& graphData = graphs[graphSym];
      auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef, identColumnRef}));
      auto* b = new Block();
      b->addArgument(tuples::TupleType::get(ctxt), loc);
      auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
      auto identArg = b->addArgument(gsubop::IdentifierType::get(ctxt), loc);
      nestedMapOp.getRegion().push_back(b);
      auto targetDef = resolveTargetDef(ct, nodeRefType);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         auto [scanIdentDef, scanIdentRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "scan");
         mlir::Value inner = generateTupleStream(rewriter, loc, scanIdentDef, [&](mlir::OpBuilder&) -> mlir::Value {
            return identArg;
         });
         inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), targetDef);
         rewriter.create<tuples::ReturnOp>(loc, inner);
      }
      stream = nestedMapOp;
      return finishClassifiedTerm(stream, ct, targetDef);
   }
   mlir::Value emitTerm(mlir::Value stream, mlir::Attribute term, mlir::StringRef pos, Member nodeMember, tuples::ColumnRefAttr edgeRef) {
      if (auto constTerm = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
         auto [def, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "id");
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constTerm.getIdent());
         stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
         return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, ident);
      }
      auto ct = classify(pos, term);
      auto targetDef = resolveTargetDef(ct, memberManager.getType(nodeMember));
      stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, targetDef}}));
      return finishClassifiedTerm(stream, ct, targetDef);
   }
}; // TripleEmitter

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
class TriplePatternLowering : public OpConversionPattern<gpm::TriplePatternOp> {
   NamedGraphMapping& graphs;
   public:
   TriplePatternLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapping& graphs)
      : OpConversionPattern<gpm::TriplePatternOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::TriplePatternOp tripleOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      TripleEmitter emitter(rewriter, graphs, tripleOp);
      rewriter.replaceOp(tripleOp, emitter.lower(adaptor.getRel()));
      return success();
   }
};
// Decays into constant 'true' because hash attributes are set.
class GpmIdentifiersEqualLowering : public OpConversionPattern<gpm::IdentifiersEqualOp> {
   public:
   using OpConversionPattern<gpm::IdentifiersEqualOp>::OpConversionPattern;
   LogicalResult matchAndRewrite(gpm::IdentifiersEqualOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.replaceOpWithNewOp<mlir::arith::ConstantOp>(op, rewriter.getBoolAttr(true));
      return success();
   }
};

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
   patterns.insert<TriplePatternLowering>(typeConverter, ctxt, graphs);
   patterns.insert<GpmIdentifiersEqualLowering>(typeConverter, ctxt);

   if (failed(applyFullConversion(module, target, std::move(patterns))))
      signalPassFailure();
}
} // namespace
std::unique_ptr<mlir::Pass>
gpm::createLowerToSubOpPass() {
   return std::make_unique<GPMToSubOpLoweringPass>();
}
void gpm::createLowerGPMToSubOpPipeline(mlir::OpPassManager& pm) {
   pm.addPass(gpm::createUnnestGraphPatternsPass());
   pm.addPass(gpm::createCreateRelAlgInFlightsPass());
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
