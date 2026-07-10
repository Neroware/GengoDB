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
struct TripleData {
   tuples::ColumnRefAttr graphRef;
   mlir::Attribute s, p, o;
};
using TripleList = std::shared_ptr<llvm::SmallVector<TripleData>>;
struct HashJoinBuild {
   mlir::Value multiMap;
   subop::MultiMapType type;
   Member keyMember, valMember;
   mlir::Type idType;
};
struct Binding {
   tuples::ColumnDefAttr def;
   TripleList triples;
   size_t originIndex;
   std::shared_ptr<HashJoinBuild> hashJoinBuild;
};
using LocalIdentifierMapping = llvm::DenseMap<mlir::StringRef, Binding>;
using VariableBinding = llvm::DenseMap<mlir::SymbolRefAttr, Binding>;
struct TripleEmitContext {
   NamedGraphMapping graphs;
   VariableBinding& bindings;
   LocalIdentifierMapping localIdents;
   const llvm::DenseSet<mlir::SymbolRefAttr>& probedVariableNames;
   llvm::DenseSet<mlir::StringRef> probedBNodeIds;
};
struct GraphDataOverrideGuard {
   NamedGraphMapping& graphs;
   mlir::SymbolRefAttr key;
   NamedGraphData saved{};
   bool active = false;
   GraphDataOverrideGuard(NamedGraphMapping& graphs, mlir::SymbolRefAttr key) : graphs(graphs), key(key) {}
   void activate(const NamedGraphData& fresh) {
      saved = graphs[key];
      graphs[key] = fresh;
      active = true;
   }
   ~GraphDataOverrideGuard() {
      if (active) graphs[key] = saved;
   }
};
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
static subop::ColumnRefMemberMappingAttr createColumnRefMemberMappingAttr(MLIRContext* context, RefMappingCollector pairs) {
   return subop::ColumnRefMemberMappingAttr::get(context, pairs);
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

inline static bool isBound(mlir::Attribute term, const LocalIdentifierMapping& localIdents) {
   if (mlir::isa<gpm::IdentifierTermAttr>(term)) return true;
   if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
      return var.hasBinding();
   if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term))
      return localIdents.count(bnode.getLocalId()) > 0;
   return false;
}

static TripleList extractTriples(gpm::BasicGraphPatternOp basicGraphPatternOp) {
   auto& block = basicGraphPatternOp.getPattern().front();
   TripleList triples = std::make_shared<llvm::SmallVector<TripleData>>();
   for (auto& op : block.without_terminator()) {
      auto triple = mlir::cast<gpm::TriplePatternOp>(&op);
      triples->push_back(TripleData{triple.getGraphRef(), triple.getS(), triple.getP(), triple.getO()});
   }
   return triples;
}
static void collectProbedNames(TripleList triples, llvm::DenseSet<mlir::SymbolRefAttr>& probedVars, llvm::DenseSet<mlir::StringRef>* probedBNodes) {
   llvm::DenseSet<mlir::StringRef> seenBNodes;
   auto isBoundLocal = [&](mlir::Attribute term) {
      if (mlir::isa<gpm::IdentifierTermAttr>(term)) 
         return true;
      if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term)) 
         return var.hasBinding();
      if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) 
         return seenBNodes.count(bnode.getLocalId()) > 0;
      return false;
   };
   auto markProbed = [&](mlir::Attribute term) {
      if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
         if (var.hasBinding()) {
            probedVars.insert(var.getBindingReference().getName());
         }
      } 
      else if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
         if (probedBNodes && seenBNodes.count(bnode.getLocalId())) {
            probedBNodes->insert(bnode.getLocalId());
         }
      }
   };
   for (auto& triple : *triples) {
      bool anchorIsSubject = isBoundLocal(triple.s);
      bool anchorIsObject = !anchorIsSubject && isBoundLocal(triple.o);
      markProbed(triple.p);
      if (anchorIsSubject) { 
         markProbed(triple.o);
      }
      else if (anchorIsObject) {
         markProbed(triple.s);
      }
      else {
         markProbed(triple.s);
         markProbed(triple.o);
      }
      for (mlir::Attribute term : {triple.s, triple.p, triple.o}) {
         if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
            seenBNodes.insert(bnode.getLocalId());
         }
      }
   }
}

template<typename ColumnAttrT>
static mlir::Value scanNamedGraph(ConversionPatternRewriter& rewriter, mlir::Location loc, ColumnAttrT graphAttr, NamedGraphMapping& graphs, bool uniqueScope = false, NamedGraphData* outData = nullptr) {
   auto ctxt = rewriter.getContext();
   auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto graphRef = graphAttr.getName();
   auto [graphGroup, graphName] = splitGraphRef(graphAttr);
   auto graphRefType = mlir::cast<gpm::GraphReferenceType>(graphAttr.getColumn().type);
   gsubop::GraphType graphType;
   tuples::ColumnDefAttr nodeSetDef, edgeSetDef;
   auto graphNameAttr = graphRefType.getName();
   auto it = graphs.find(graphRef);
   if (it != graphs.end() && !uniqueScope) {
      graphType = it->second.graphType;
      nodeSetDef = columnManager.createDef(it->second.nodeSetColumn);
      edgeSetDef = columnManager.createDef(it->second.edgeSetColumn);
   }
   else if (it != graphs.end()) {
      graphType = it->second.graphType;
      nodeSetDef = createDef(columnManager, graphGroup, graphName + "_vx", it->second.nodeSetColumn->type, true);
      edgeSetDef = createDef(columnManager, graphGroup, graphName + "_ex", it->second.edgeSetColumn->type, true);
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
   if (outData) *outData = NamedGraphData{graphType, &nodeSetDef.getColumn(), &edgeSetDef.getColumn()};
   mlir::Value externalGraph = rewriter.create<gsubop::GetExternalGraphOp>(loc, graphType, graphNameAttr, graphRefType.getGlobalId());
   return rewriter.create<gsubop::ScanGraphOp>(loc, externalGraph, nodeSetDef, edgeSetDef);
}

class NamedGraphLowering : public OpConversionPattern<gpm::NamedGraphOp> {
   NamedGraphMapping& graphs;
   public:
   NamedGraphLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapping& graphs)
      : OpConversionPattern<gpm::NamedGraphOp>(typeConverter, context), graphs(graphs) {}
   LogicalResult matchAndRewrite(gpm::NamedGraphOp namedGraphOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.replaceOp(namedGraphOp, scanNamedGraph(rewriter, namedGraphOp->getLoc(), namedGraphOp.getDef(), graphs));
      return success();
   }
};

class TriplePatternEmitter {
   ConversionPatternRewriter& rewriter;
   MLIRContext* ctxt;
   tuples::ColumnManager& columnManager;
   subop::MemberManager& memberManager;
   public:
   TriplePatternEmitter(ConversionPatternRewriter& rewriter)
      : rewriter(rewriter), ctxt(rewriter.getContext()),
      columnManager(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()),
      memberManager(ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager()) {}
   static bool reusesExistingBinding(mlir::Attribute term, const TripleEmitContext& emitCtxt) {
      if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
         return var.hasBinding();
      if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term))
         return emitCtxt.localIdents.count(bnode.getLocalId()) > 0;
      return false;
   }
   bool needsProbeRestart(const TripleData& triple, bool anchorIsSubject, bool anchorIsObject, const TripleEmitContext& emitCtxt) const {
      if (reusesExistingBinding(triple.p, emitCtxt)) return true;
      if (anchorIsSubject) return reusesExistingBinding(triple.o, emitCtxt);
      if (anchorIsObject) return reusesExistingBinding(triple.s, emitCtxt);
      return reusesExistingBinding(triple.s, emitCtxt) || reusesExistingBinding(triple.o, emitCtxt);
   }
   mlir::Value lowerTriple(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
      auto& triple = (*triples)[index];
      bool anchorIsSubject = isBound(triple.s, emitCtxt.localIdents);
      bool anchorIsObject = !anchorIsSubject && isBound(triple.o, emitCtxt.localIdents);
      if (anchorIsSubject)
         stream = lowerSubjectFirst(loc, stream, triples, index, emitCtxt);
      else if (anchorIsObject)
         stream = lowerObjectFirst(loc, stream, triples, index, emitCtxt);
      else
         stream = lowerPredicateFirst(loc, stream, triples, index, emitCtxt);
      return stream;
   }
   private:
   mlir::Value resolveAnchorNode(mlir::Location loc, mlir::Value stream, mlir::Attribute term, tuples::ColumnRefAttr graphRefAttr, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef, TripleEmitContext& emitCtxt) const {
      auto graphRef = graphRefAttr.getName();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      if (auto identTermAttr = mlir::dyn_cast_or_null<gpm::IdentifierTermAttr>(term)) {
         auto& graphData = emitCtxt.graphs[graphRef];
         auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
         auto identState = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, identTermAttr.getIdent());
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef}));
         auto* b = new Block();
         b->addArgument(tuples::TupleType::get(ctxt), loc);
         auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
         nestedMapOp.getRegion().push_back(b);
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(b);
            auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
            auto scan = rewriter.create<gsubop::ScanIdentifierOp>(loc, identState, identDef);
            nodeRefType = createNodeRefType(ctxt, group, graph);
            auto [nodeDef, resolvedRef] = createColumn(nodeRefType, "nodes", "ref");
            mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({identRef}), nodeDef);
            rewriter.create<tuples::ReturnOp>(loc, lookup);
            nodeRef = resolvedRef;
         }
         return nestedMapOp.getRes();
      }
      if (auto varTermAttr = mlir::dyn_cast_or_null<gpm::VariableTermAttr>(term)) {
         auto bindingDef = emitCtxt.bindings[varTermAttr.getBindingReference().getName()].def;
         nodeRef = columnManager.createRef(bindingDef.getColumnPtr().get());
         nodeRefType = mlir::cast<gsubop::NodeRefType>(bindingDef.getColumn().type);
         return stream;
      }
      if (auto bnodeTermAttr = mlir::dyn_cast_or_null<gpm::BNodeTermAttr>(term)) {
         auto bindingDef = emitCtxt.localIdents[bnodeTermAttr.getLocalId()].def;
         nodeRef = columnManager.createRef(bindingDef.getColumnPtr().get());
         nodeRefType = mlir::cast<gsubop::NodeRefType>(bindingDef.getColumn().type);
         return stream;
      }
      return stream;
   }
   mlir::Value scanEdges(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr edgesRef, mlir::Type edgeSetType, const std::string& group, const std::string& graph, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef) const {
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
   Block* buildEqBlock(mlir::Location loc, mlir::Type type) const {
      auto* block = new Block();
      auto lhs = block->addArgument(type, loc);
      auto rhs = block->addArgument(type, loc);
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(block);
      mlir::Value cmp = rewriter.create<db::CmpOp>(loc, db::DBCmpPredicate::eq, lhs, rhs);
      rewriter.create<tuples::ReturnOp>(loc, cmp);
      return block;
   }
   void buildHashIndex(mlir::Location loc, mlir::Value stream, Binding& binding, TripleList triples, size_t index) const {
      if (binding.hashJoinBuild) return;
      binding.triples = triples;
      binding.originIndex = index;
      auto nodeRefType = mlir::cast<gsubop::NodeRefType>(binding.def.getColumn().type);
      auto idMember = nodeRefType.getNodeMembers().getMembers()[0];
      auto idType = memberManager.getType(idMember);
      auto bindingRef = columnManager.createRef(binding.def.getColumnPtr().get());
      auto [idDef, idRef] = createColumn(idType, "hj", "id");
      stream = rewriter.create<subop::GatherOp>(loc, stream, bindingRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, idDef}}));
      auto keyMember = createMember(ctxt, "hjkey", idType);
      auto valMember = createMember(ctxt, "hjval", idType);
      auto multiMapType = subop::MultiMapType::get(ctxt, createStateMembersAttr(ctxt, {keyMember}), createStateMembersAttr(ctxt, {valMember}));
      mlir::Value multiMap = rewriter.create<subop::GenericCreateOp>(loc, multiMapType);
      auto insertOp = rewriter.create<subop::InsertOp>(loc, stream, multiMap, createColumnRefMemberMappingAttr(ctxt, {{keyMember, idRef}, {valMember, idRef}}));
      insertOp.getEqFn().push_back(buildEqBlock(loc, idType));
      binding.hashJoinBuild = std::make_shared<HashJoinBuild>(HashJoinBuild{multiMap, multiMapType, keyMember, valMember, idType});
   }
   mlir::Value probeHashJoin(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr candidateIdRef, Binding& target, tuples::ColumnRefAttr vxRef, mlir::Type vxType) const {
      auto& hj = *target.hashJoinBuild;
      auto entryRefType = subop::MultiMapEntryRefType::get(ctxt, hj.type);
      auto listType = subop::ListType::get(ctxt, entryRefType);
      auto [listDef, listRef] = createColumn(listType, "hj", "list");
      auto lookupOp = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), stream, hj.multiMap, rewriter.getArrayAttr({candidateIdRef}), listDef);
      lookupOp.getEqFn().push_back(buildEqBlock(loc, hj.idType));
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), lookupOp.getRes(), rewriter.getArrayAttr({listRef, vxRef}));
      auto* b = new Block();
      auto tupleArg = b->addArgument(tuples::TupleType::get(ctxt), loc);
      b->addArgument(listType, loc);
      auto vxArg = b->addArgument(vxType, loc);
      nestedMapOp.getRegion().push_back(b);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         auto [entryDef, entryRef] = createColumn(entryRefType, "hj", "entryref");
         mlir::Value inner = rewriter.create<subop::ScanListOp>(loc, b->getArgument(1), entryDef);
         auto [valDef, valRef] = createColumn(hj.idType, "hj", "matched");
         inner = rewriter.create<subop::GatherOp>(loc, inner, entryRef, createColumnDefMemberMappingAttr(ctxt, {{hj.valMember, valDef}}));
         auto reconstructedDef = createDef(columnManager, target.def.getName().getRootReference().str(), target.def.getName().getLeafReference().str(), target.def.getColumn().type, false);
         inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, vxArg, rewriter.getArrayAttr({valRef}), reconstructedDef);
         mlir::Value combined = rewriter.create<subop::CombineTupleOp>(loc, inner, tupleArg);
         rewriter.create<tuples::ReturnOp>(loc, combined);
      }
      return nestedMapOp.getRes();
   }
   mlir::Value lowerSubjectFirst(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
      auto& triple = (*triples)[index];
      auto graphRefAttr = triple.graphRef;
      auto [group, graph] = splitGraphRef(graphRefAttr);
      GraphDataOverrideGuard graphGuard(emitCtxt.graphs, graphRefAttr.getName());
      if (needsProbeRestart(triple, true, false, emitCtxt)) {
         NamedGraphData fresh;
         stream = scanNamedGraph(rewriter, loc, graphRefAttr, emitCtxt.graphs, true, &fresh);
         graphGuard.activate(fresh);
      }
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      stream = resolveAnchorNode(loc, stream, triple.s, graphRefAttr, nodeRefType, nodeRef, emitCtxt);
      auto edgeSetType = memberManager.getType(nodeRefType.getOutgoingMembers().getMembers()[0]);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "outgoing");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getOutgoingMembers().getMembers()[0], edgesDef}}));
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRefColumnRef;
      stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
      stream = lowerPredicate(loc, stream, graphRefAttr, triple.p, edgeRefColumnRef, columnManager, emitCtxt, triples, index);
      stream = lowerTerm(loc, stream, triple.o, edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt, triples, index);
      return stream;
   }
   mlir::Value lowerObjectFirst(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
      auto& triple = (*triples)[index];
      auto graphRefAttr = triple.graphRef;
      auto [group, graph] = splitGraphRef(graphRefAttr);
      GraphDataOverrideGuard graphGuard(emitCtxt.graphs, graphRefAttr.getName());
      if (needsProbeRestart(triple, false, true, emitCtxt)) {
         NamedGraphData fresh;
         stream = scanNamedGraph(rewriter, loc, graphRefAttr, emitCtxt.graphs, true, &fresh);
         graphGuard.activate(fresh);
      }
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      stream = resolveAnchorNode(loc, stream, triple.o, graphRefAttr, nodeRefType, nodeRef, emitCtxt);
      auto edgeSetType = memberManager.getType(nodeRefType.getIncomingMembers().getMembers()[0]);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "incoming");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getIncomingMembers().getMembers()[0], edgesDef}}));
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRefColumnRef;
      stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
      stream = lowerPredicate(loc, stream, graphRefAttr, triple.p, edgeRefColumnRef, columnManager, emitCtxt, triples, index);
      stream = lowerTerm(loc, stream, triple.s, edgeRefType.getFromMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt, triples, index);
      return stream;
   }
   mlir::Value lowerPredicateFirst(mlir::Location loc, mlir::Value stream, TripleList triples, size_t index, TripleEmitContext& emitCtxt) const {
      auto& triple = (*triples)[index];
      auto graphRefAttr = triple.graphRef;
      auto [group, graph] = splitGraphRef(graphRefAttr);
      GraphDataOverrideGuard graphGuard(emitCtxt.graphs, graphRefAttr.getName());
      if (needsProbeRestart(triple, false, false, emitCtxt)) {
         NamedGraphData fresh;
         stream = scanNamedGraph(rewriter, loc, graphRefAttr, emitCtxt.graphs, true, &fresh);
         graphGuard.activate(fresh);
      }
      auto& graphData = emitCtxt.graphs[graphRefAttr.getName()];
      auto edgesRef = columnManager.createRef(graphData.edgeSetColumn);
      auto edgeSetType = graphData.edgeSetColumn->type;
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRefColumnRef;
      stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
      stream = lowerPredicate(loc, stream, graphRefAttr, triple.p, edgeRefColumnRef, columnManager, emitCtxt, triples, index);
      stream = lowerTerm(loc, stream, triple.s, edgeRefType.getFromMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt, triples, index);
      stream = lowerTerm(loc, stream, triple.o, edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, emitCtxt, triples, index);
      return stream;
   }
   mlir::Value lowerPredicate(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr graphRefAttr, mlir::Attribute p, tuples::ColumnRefAttr edgeRef, tuples::ColumnManager& columnManager, TripleEmitContext& emitCtxt, TripleList triples, size_t index) const {
      auto graphRef = graphRefAttr.getName();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      if (auto constPred = mlir::dyn_cast<gpm::IdentifierTermAttr>(p)) {
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constPred.getIdent());
         stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, ident);
      }
      else if (auto varPred = mlir::dyn_cast<gpm::VariableTermAttr>(p)) {
         auto [identColumnDef, identColumnRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "map");
         stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, edgeRef, identColumnDef);
         auto nodeRefType = createNodeRefType(ctxt, group, graph);
         auto& graphData = emitCtxt.graphs[graphRef];
         auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef, identColumnRef}));
         auto* b = new Block();
         b->addArgument(tuples::TupleType::get(ctxt), loc);
         auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
         auto identArg = b->addArgument(gsubop::IdentifierType::get(ctxt), loc);
         nestedMapOp.getRegion().push_back(b);
         tuples::ColumnRefAttr predNodeRef;
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(b);
            auto [scanIdentDef, scanIdentRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "scan");
            mlir::Value inner = rewriter.create<gsubop::ScanIdentifierOp>(loc, identArg, scanIdentDef);
            if (varPred.hasBinding()) {
               auto [predNodeDef, predNodeRefLocal] = createColumn(nodeRefType, "hj", "prednode");
               inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), predNodeDef);
               auto idMember = nodeRefType.getNodeMembers().getMembers()[0];
               auto [candidateIdDef, candidateIdRef] = createColumn(memberManager.getType(idMember), "hj", "candidate");
               inner = rewriter.create<subop::GatherOp>(loc, inner, predNodeRefLocal, createColumnDefMemberMappingAttr(ctxt, {{idMember, candidateIdDef}}));
               predNodeRef = candidateIdRef;
            } 
            else {
               auto ref = varPred.getProducedBinding();
               auto def = createDef(columnManager, ref.getName().getRootReference().str(), ref.getName().getLeafReference().str(), nodeRefType, false);
               emitCtxt.bindings[ref.getName()].def = def;
               inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), def);
            }
            rewriter.create<tuples::ReturnOp>(loc, inner);
         }
         stream = nestedMapOp.getRes();
         if (varPred.hasBinding()) {
            auto& target = emitCtxt.bindings[varPred.getBindingReference().getName()];
            stream = probeHashJoin(loc, stream, predNodeRef, target, nodesRef, graphData.nodeSetColumn->type);
         }
         else if (emitCtxt.probedVariableNames.contains(varPred.getProducedBinding().getName())) {
            buildHashIndex(loc, stream, emitCtxt.bindings[varPred.getProducedBinding().getName()], triples, index);
         }
      }
      return stream;
   }
   mlir::Value lowerTerm(mlir::Location loc, mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::string graph, TripleEmitContext& emitCtxt, TripleList triples, size_t index) const {
      auto ctxt = rewriter.getContext();
      auto& memberManager = ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
      if (auto constTerm = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
         auto [def, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "id");
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constTerm.getIdent());
         stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
         stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, ident);
      }
      else if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term)) {
         if (varTerm.hasBinding()) {
            auto [nodeRefColumnDef, nodeRefColumnRef] = createColumn(memberManager.getType(nodeMember), "nodes", "ref");
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, nodeRefColumnDef}}));
            auto candidateNodeRefType = mlir::cast<gsubop::NodeRefType>(memberManager.getType(nodeMember));
            auto idMember = candidateNodeRefType.getNodeMembers().getMembers()[0];
            auto [candidateIdDef, candidateIdRef] = createColumn(memberManager.getType(idMember), "hj", "candidate");
            stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRefColumnRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, candidateIdDef}}));
            auto& target = emitCtxt.bindings[varTerm.getBindingReference().getName()];
            auto& graphData = emitCtxt.graphs[(*triples)[index].graphRef.getName()];
            stream = probeHashJoin(loc, stream, candidateIdRef, target, columnManager.createRef(graphData.nodeSetColumn), graphData.nodeSetColumn->type);
         }
         else {
            auto ref = varTerm.getProducedBinding();
            auto def = createDef(columnManager, ref.getName().getRootReference().str(), ref.getName().getLeafReference().str(), memberManager.getType(nodeMember), false);
            emitCtxt.bindings[ref.getName()].def = def;
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
            if (emitCtxt.probedVariableNames.contains(ref.getName())) {
               buildHashIndex(loc, stream, emitCtxt.bindings[ref.getName()], triples, index);
            }
         }
      }
      else if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
         if (emitCtxt.localIdents.count(bnode.getLocalId()) > 0) {
            auto [nodeRefColumnDef, nodeRefColumnRef] = createColumn(memberManager.getType(nodeMember), "nodes", "ref");
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, nodeRefColumnDef}}));
            auto candidateNodeRefType = mlir::cast<gsubop::NodeRefType>(memberManager.getType(nodeMember));
            auto idMember = candidateNodeRefType.getNodeMembers().getMembers()[0];
            auto [candidateIdDef, candidateIdRef] = createColumn(memberManager.getType(idMember), "hj", "candidate");
            stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRefColumnRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, candidateIdDef}}));
            auto& target = emitCtxt.localIdents[bnode.getLocalId()];
            auto& graphData = emitCtxt.graphs[(*triples)[index].graphRef.getName()];
            stream = probeHashJoin(loc, stream, candidateIdRef, target, columnManager.createRef(graphData.nodeSetColumn), graphData.nodeSetColumn->type);
         }
         else {
            auto def = createDef(columnManager, "bnode", bnode.localId(), memberManager.getType(nodeMember), true);
            emitCtxt.localIdents[bnode.getLocalId()].def = def;
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
            if (emitCtxt.probedBNodeIds.contains(bnode.getLocalId())) {
               buildHashIndex(loc, stream, emitCtxt.localIdents[bnode.getLocalId()], triples, index);
            }
         }
      }
      return stream;
   }
};

class BasicGraphPatternLowering : public OpConversionPattern<gpm::BasicGraphPatternOp> {
   NamedGraphMapping& graphs;
   VariableBinding& bindings;
   const llvm::DenseSet<mlir::SymbolRefAttr>& probedVariableNames;
   public:
   BasicGraphPatternLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapping& graphs, VariableBinding& bindings, const llvm::DenseSet<mlir::SymbolRefAttr>& probedVariableNames)
      : OpConversionPattern<gpm::BasicGraphPatternOp>(typeConverter, context), graphs(graphs), bindings(bindings), probedVariableNames(probedVariableNames) {}
   LogicalResult matchAndRewrite(gpm::BasicGraphPatternOp basicGraphPatternOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      TripleList triples = extractTriples(basicGraphPatternOp);
      llvm::DenseSet<mlir::SymbolRefAttr> unusedVars;
      llvm::DenseSet<mlir::StringRef> probedBNodeIds;
      collectProbedNames(triples, unusedVars, &probedBNodeIds);
      rewriter.setInsertionPoint(basicGraphPatternOp);
      mlir::Value stream = adaptor.getRel();
      TripleEmitContext emitCtxt{graphs, bindings, LocalIdentifierMapping(), probedVariableNames, std::move(probedBNodeIds)};
      TriplePatternEmitter emitter(rewriter);
      auto loc = basicGraphPatternOp->getLoc();
      for (size_t i = 0; i < triples->size(); ++i) {
         stream = emitter.lowerTriple(loc, stream, triples, i, emitCtxt);
      }
      rewriter.replaceOp(basicGraphPatternOp, stream);
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
   auto* ctxt = &getContext();
   ctxt->loadDialect<gsubop::GraphSubOpDialect>();
   RewritePatternSet patterns(ctxt);

   VariableBinding bindings;
   NamedGraphMapping graphs;

   llvm::DenseSet<mlir::SymbolRefAttr> probedVariableNames;
   module.walk([&](gpm::BasicGraphPatternOp basicGraphPatternOp) {
      collectProbedNames(extractTriples(basicGraphPatternOp), probedVariableNames, nullptr);
   });

   patterns.insert<NamedGraphLowering>(typeConverter, ctxt, graphs);
   patterns.insert<BasicGraphPatternLowering>(typeConverter, ctxt, graphs, bindings, probedVariableNames);

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
   pm.addPass(gpm::createStringifyMaterializedGraphRefsPass());
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
