#include "gengodb/compiler/Conversion/GPMToSubOp/GPMToSubOpPass.h"

#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
#include "lingodb/compiler/Conversion/RelAlgToSubOp/OrderedAttributes.h"
#include "lingodb/compiler/Dialect/Arrow/IR/ArrowDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsInterfaces.h"
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
#include <optional>

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
struct HashJoinBuild {
   mlir::Value multiMap;
   subop::MultiMapType type;
   Member keyMember;
   llvm::SmallVector<std::pair<tuples::ColumnDefAttr, Member>> valueMembers;
   mlir::Type idType;
};
struct Binding {
   tuples::ColumnDefAttr def;
   mlir::Operation* originTriple = nullptr;
   std::shared_ptr<HashJoinBuild> hashJoinBuild;
};
using VariableBinding = llvm::DenseMap<mlir::SymbolRefAttr, Binding>;
struct TripleEmitContext {
   NamedGraphMapping& graphs;
   VariableBinding& bindings;
   const llvm::DenseSet<mlir::SymbolRefAttr>& probedNames;
};
struct AnchorSelection {
   bool anchorIsSubject;
   bool anchorIsObject;
   bool restart;
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
static std::optional<mlir::SymbolRefAttr> resolveBindingName(mlir::Attribute term, gpm::TriplePatternOp triple) {
   if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
      return var.hasBinding() ? var.getBindingReference().getName() : var.getProducedBinding().getName();
   if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
      auto scope = triple.getBNodeScope();
      if (!scope) return std::nullopt;
      auto entry = scope.get(bnode.getLocalId().getValue());
      if (!entry) return std::nullopt;
      return mlir::cast<tuples::ColumnRefAttr>(entry).getName();
   }
   return std::nullopt;
}
template<typename IdentMappingT>
inline static bool isBound(mlir::Attribute term, gpm::TriplePatternOp triple, const IdentMappingT& identMapping) {
   if (mlir::isa<gpm::IdentifierTermAttr>(term)) return true;
   if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
      return var.hasBinding();
   if (mlir::isa<gpm::BNodeTermAttr>(term)) {
      auto name = resolveBindingName(term, triple);
      return name && identMapping.count(*name) > 0;
   }
   return false;
}
template<typename IdentMappingT>
static bool reusesExistingBinding(mlir::Attribute term, gpm::TriplePatternOp triple, const IdentMappingT& identMapping) {
   if (auto var = mlir::dyn_cast<gpm::VariableTermAttr>(term))
      return var.hasBinding();
   if (mlir::isa<gpm::BNodeTermAttr>(term)) {
      auto name = resolveBindingName(term, triple);
      return name && identMapping.count(*name) > 0;
   }
   return false;
}
template<typename IdentMappingT>
static bool reusesHashBinding(mlir::Attribute term, gpm::TriplePatternOp triple, const std::string& joinStrategy, const IdentMappingT& identMapping) {
   if (joinStrategy != "hash") return false;
   return reusesExistingBinding(term, triple, identMapping);
}
template<typename IdentMappingT>
static bool needsProbeRestart(gpm::TriplePatternOp triple, bool anchorIsSubject, bool anchorIsObject, const IdentMappingT& identMapping) {
   auto joinStrategy = triple.getJoinStrategy();
   if (reusesHashBinding(triple.getP(), triple, joinStrategy, identMapping)) return true;
   if (anchorIsSubject) return reusesHashBinding(triple.getO(), triple, joinStrategy, identMapping);
   if (anchorIsObject) return reusesHashBinding(triple.getS(), triple, joinStrategy, identMapping);
   return reusesHashBinding(triple.getS(), triple, joinStrategy, identMapping) || reusesHashBinding(triple.getO(), triple, joinStrategy, identMapping);
}
template<typename IdentMappingT>
static AnchorSelection selectAnchor(gpm::TriplePatternOp triple, const IdentMappingT& identMapping) {
   bool anchorIsSubject = isBound(triple.getS(), triple, identMapping);
   bool anchorIsObject = !anchorIsSubject && isBound(triple.getO(), triple, identMapping);
   bool restart = needsProbeRestart(triple, anchorIsSubject, anchorIsObject, identMapping);
   if (restart) {
      anchorIsSubject = mlir::isa<gpm::IdentifierTermAttr>(triple.getS());
      anchorIsObject = !anchorIsSubject && mlir::isa<gpm::IdentifierTermAttr>(triple.getO());
   }
   return {anchorIsSubject, anchorIsObject, restart};
}
static void collectProbedNames(mlir::ModuleOp module, llvm::DenseSet<mlir::SymbolRefAttr>& probedNames) {
   llvm::DenseSet<mlir::SymbolRefAttr> seen;
   module.walk([&](gpm::TriplePatternOp triple) {
      auto markProbed = [&](mlir::Attribute term) {
         if (!reusesExistingBinding(term, triple, seen)) 
            return;
         if (auto name = resolveBindingName(term, triple)) 
            probedNames.insert(*name);
      };
      auto sel = selectAnchor(triple, seen);
      markProbed(triple.getP());
      if (sel.anchorIsSubject) {
         markProbed(triple.getO());
      } 
      else if (sel.anchorIsObject) {
         markProbed(triple.getS());
      } 
      else {
         markProbed(triple.getS());
         markProbed(triple.getO());
      }
      for (mlir::Attribute term : {triple.getS(), triple.getP(), triple.getO()}) {
         if (mlir::isa<gpm::BNodeTermAttr>(term)) {
            if (auto name = resolveBindingName(term, triple)) seen.insert(*name);
         }
      }
   });
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
static llvm::SmallVector<mlir::SymbolRefAttr, 3> collectOriginSiblings(gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt) {
   llvm::SmallVector<mlir::SymbolRefAttr, 3> result;
   if (triple.getJoinStrategy() != "hash") return result;
   auto consider = [&](mlir::Attribute term) {
      auto name = resolveBindingName(term, triple);
      if (!name) return;
      auto it = emitCtxt.bindings.find(*name);
      if (it != emitCtxt.bindings.end() && it->second.originTriple == triple.getOperation())
         result.push_back(*name);
   };
   consider(triple.getP());
   consider(triple.getS());
   consider(triple.getO());
   return result;
}
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
      rewriter.replaceOp(namedGraphOp, scanNamedGraph(rewriter, namedGraphOp->getLoc(), namedGraphOp.getDef(), graphs, true));
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
   mlir::Value lowerTriple(mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt) const {
      auto sel = selectAnchor(triple, emitCtxt.bindings);
      if (sel.anchorIsSubject)
         stream = lowerSubjectFirst(loc, stream, triple, emitCtxt, sel.restart);
      else if (sel.anchorIsObject)
         stream = lowerObjectFirst(loc, stream, triple, emitCtxt, sel.restart);
      else
         stream = lowerPredicateFirst(loc, stream, triple, emitCtxt, sel.restart);
      return stream;
   }
   private:
   struct UnwrappedBinding {
      mlir::Value stream;
      tuples::ColumnRefAttr ref;
      mlir::Type type;
   };
   private:
   UnwrappedBinding unwrapNullableBinding(mlir::Location loc, mlir::Value stream, tuples::ColumnDefAttr bindingDef) const {
      auto rawType = bindingDef.getColumn().type;
      auto bindingRef = columnManager.createRef(bindingDef.getColumnPtr().get());
      auto nullableType = mlir::dyn_cast<db::NullableType>(rawType);
      if (!nullableType) return {stream, bindingRef, rawType};
      auto innerType = nullableType.getType();
      auto [isNullDef, isNullRef] = createColumn(rewriter.getI1Type(), "opt", "isnull");
      stream = rewriter.create<gsubop::IsNullRefOp>(loc, stream, bindingRef, isNullDef);
      auto [valDef, valRef] = createColumn(innerType, "opt", "val");
      stream = rewriter.create<gsubop::UnwrapNullableRefOp>(loc, stream, bindingRef, valDef);
      stream = rewriter.create<subop::FilterOp>(loc, stream, subop::FilterSemantic::none_true, rewriter.getArrayAttr({isNullRef}));
      return {stream, valRef, innerType};
   }
   mlir::Value resolveAnchorNode(mlir::Location loc, mlir::Value stream, mlir::Attribute term, gpm::TriplePatternOp triple, tuples::ColumnRefAttr graphRefAttr, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef, TripleEmitContext& emitCtxt) const {
      auto graphRef = graphRefAttr.getName();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      if (auto identTermAttr = mlir::dyn_cast_or_null<gpm::IdentifierTermAttr>(term)) {
         auto& graphData = emitCtxt.graphs[graphRef];
         auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef}));
         auto* b = new Block();
         b->addArgument(tuples::TupleType::get(ctxt), loc);
         auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
         nestedMapOp.getRegion().push_back(b);
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(b);
            auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
            auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& b) -> mlir::Value {
               return b.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, identTermAttr.getIdent());
            });
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
         auto unwrapped = unwrapNullableBinding(loc, stream, bindingDef);
         nodeRef = unwrapped.ref;
         nodeRefType = mlir::cast<gsubop::NodeRefType>(unwrapped.type);
         return unwrapped.stream;
      }
      if (mlir::isa_and_nonnull<gpm::BNodeTermAttr>(term)) {
         auto name = resolveBindingName(term, triple);
         auto bindingDef = emitCtxt.bindings[*name].def;
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
   void ensureJointHashIndex(mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt) const {
      auto siblings = collectOriginSiblings(triple, emitCtxt);
      size_t keyIdx = siblings.size();
      for (size_t i = 0; i < siblings.size(); ++i) {
         if (emitCtxt.probedNames.contains(siblings[i])) {
            keyIdx = i;
            break;
         }
      }
      if (keyIdx == siblings.size()) return;
      llvm::SmallVector<std::pair<tuples::ColumnDefAttr, tuples::ColumnRefAttr>> idCols;
      mlir::Type idType;
      for (auto& name : siblings) {
         Binding& binding = emitCtxt.bindings[name];
         auto nodeRefType = mlir::cast<gsubop::NodeRefType>(binding.def.getColumn().type);
         auto idMember = nodeRefType.getNodeMembers().getMembers()[0];
         idType = memberManager.getType(idMember);
         auto bindingRef = columnManager.createRef(binding.def.getColumnPtr().get());
         auto [idDef, idRef] = createColumn(idType, "hj", "id");
         stream = rewriter.create<subop::GatherOp>(loc, stream, bindingRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, idDef}}));
         idCols.push_back({binding.def, idRef});
      }
      auto keyMember = createMember(ctxt, "hjkey", idType);
      RefMappingCollector insertMapping;
      llvm::SmallVector<Member> valMembers;
      llvm::SmallVector<std::pair<tuples::ColumnDefAttr, Member>> valueMembers;
      for (auto& [def, idRef] : idCols) {
         auto valMember = createMember(ctxt, "hjval", idType);
         valMembers.push_back(valMember);
         insertMapping.push_back({valMember, idRef});
         valueMembers.push_back({def, valMember});
      }
      insertMapping.push_back({keyMember, idCols[keyIdx].second});
      auto multiMapType = subop::MultiMapType::get(ctxt, createStateMembersAttr(ctxt, {keyMember}), createStateMembersAttr(ctxt, valMembers));
      mlir::Value multiMap = rewriter.create<subop::GenericCreateOp>(loc, multiMapType);
      auto insertOp = rewriter.create<subop::InsertOp>(loc, stream, multiMap, createColumnRefMemberMappingAttr(ctxt, insertMapping));
      insertOp.getEqFn().push_back(buildEqBlock(loc, idType));
      auto hj = std::make_shared<HashJoinBuild>(HashJoinBuild{multiMap, multiMapType, keyMember, valueMembers, idType});
      for (auto& name : siblings) {
         emitCtxt.bindings[name].hashJoinBuild = hj;
      }
   }
   mlir::Value probeHashJoin(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr candidateIdRef, HashJoinBuild& hj, tuples::ColumnRefAttr vxRef, mlir::Type vxType) const {
      auto entryRefType = subop::MultiMapEntryRefType::get(ctxt, hj.type);
      auto listType = subop::ListType::get(ctxt, entryRefType);
      auto [listDef, listRef] = createColumn(listType, "hj", "list");
      auto lookupOp = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), stream, hj.multiMap, rewriter.getArrayAttr({candidateIdRef}), listDef);
      lookupOp.getEqFn().push_back(buildEqBlock(loc, hj.idType));
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), lookupOp.getRes(), rewriter.getArrayAttr({listRef, vxRef}));
      auto* b = new Block();
      auto tupleArg = b->addArgument(tuples::TupleType::get(ctxt), loc);
      auto listArg = b->addArgument(listType, loc);
      auto vxArg = b->addArgument(vxType, loc);
      nestedMapOp.getRegion().push_back(b);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(b);
         auto [entryDef, entryRef] = createColumn(entryRefType, "hj", "entryref");
         mlir::Value inner = rewriter.create<subop::ScanListOp>(loc, listArg, entryDef);
         DefMappingCollector idMapping;
         llvm::SmallVector<std::pair<tuples::ColumnDefAttr, tuples::ColumnRefAttr>> idCols;
         for (auto& [def, member] : hj.valueMembers) {
            auto [idDef, idRef] = createColumn(hj.idType, "hj", "matched");
            idMapping.push_back({member, idDef});
            idCols.push_back({def, idRef});
         }
         inner = rewriter.create<subop::GatherOp>(loc, inner, entryRef, createColumnDefMemberMappingAttr(ctxt, idMapping));
         for (auto& [def, idRef] : idCols) {
            auto reconstructedDef = createDef(columnManager, def.getName().getRootReference().str(), def.getName().getLeafReference().str(), def.getColumn().type, false);
            inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, vxArg, rewriter.getArrayAttr({idRef}), reconstructedDef);
         }
         mlir::Value combined = rewriter.create<subop::CombineTupleOp>(loc, inner, tupleArg);
         rewriter.create<tuples::ReturnOp>(loc, combined);
      }
      return nestedMapOp.getRes();
   }
   mlir::Value filterAgainstReconstructed(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr candidateIdRef, Binding& target) const {
      auto nodeRefType = mlir::cast<gsubop::NodeRefType>(target.def.getColumn().type);
      auto idMember = nodeRefType.getNodeMembers().getMembers()[0];
      auto idType = memberManager.getType(idMember);
      auto bindingRef = columnManager.createRef(target.def.getColumnPtr().get());
      auto [existingIdDef, existingIdRef] = createColumn(idType, "hj", "existing");
      stream = rewriter.create<subop::GatherOp>(loc, stream, bindingRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, existingIdDef}}));
      auto [filterDef, filterRef] = createColumn(rewriter.getI1Type(), "hj", "eq");
      auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({filterDef}), rewriter.getArrayAttr({candidateIdRef, existingIdRef}));
      Block* mapBlock = new Block;
      auto lhs = mapBlock->addArgument(idType, loc);
      auto rhs = mapBlock->addArgument(idType, loc);
      mapOp.getRegion().push_back(mapBlock);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(mapBlock);
         mlir::Value cmp = rewriter.create<db::CmpOp>(loc, db::DBCmpPredicate::eq, lhs, rhs);
         rewriter.create<tuples::ReturnOp>(loc, cmp);
      }
      stream = mapOp.getResult();
      return rewriter.create<subop::FilterOp>(loc, stream, subop::FilterSemantic::all_true, rewriter.getArrayAttr({filterRef}));
   }
   mlir::Value filterMatchingIdentifiers(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr leftSource, tuples::ColumnRefAttr rightSource, subop::FilterSemantic semantic) const {
      auto [leftDef, leftRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "get");
      auto [rightDef, rightRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "get");
      auto [filterDef, filterRef] = createColumn(rewriter.getI1Type(), "map", "ident");
      stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, leftSource, leftDef);
      stream = rewriter.create<gsubop::GetIdentifierOp>(loc, stream, rightSource, rightDef);
      auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({filterDef}), rewriter.getArrayAttr({leftRef, rightRef}));
      Block* mapBlock = new Block;
      auto left = mapBlock->addArgument(rewriter.getI32Type(), loc);
      auto right = mapBlock->addArgument(rewriter.getI32Type(), loc);
      mapOp.getRegion().push_back(mapBlock);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(mapBlock);
         mlir::Value val = rewriter.create<arith::CmpIOp>(loc, rewriter.getI1Type(), mlir::arith::CmpIPredicate::eq, left, right);
         rewriter.create<tuples::ReturnOp>(loc, val);
      }
      stream = mapOp.getResult();
      return rewriter.create<subop::FilterOp>(loc, stream, semantic, rewriter.getArrayAttr({filterRef}));
   }
   mlir::Value probeOrFilter(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr candidateIdRef, Binding& target, tuples::ColumnRefAttr vxRef, mlir::Type vxType, llvm::DenseSet<mlir::Value>& joinedMultiMaps) const {
      if (!target.hashJoinBuild)
         return filterAgainstReconstructed(loc, stream, candidateIdRef, target);
      auto& hj = *target.hashJoinBuild;
      if (joinedMultiMaps.insert(hj.multiMap).second)
         return probeHashJoin(loc, stream, candidateIdRef, hj, vxRef, vxType);
      return filterAgainstReconstructed(loc, stream, candidateIdRef, target);
   }
   mlir::Value lowerSubjectFirst(mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt, bool restart) const {
      auto graphRefAttr = triple.getGraphRef();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      GraphDataOverrideGuard graphGuard(emitCtxt.graphs, graphRefAttr.getName());
      if (restart) {
         NamedGraphData fresh;
         stream = scanNamedGraph(rewriter, loc, graphRefAttr, emitCtxt.graphs, true, &fresh);
         graphGuard.activate(fresh);
      }
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      stream = resolveAnchorNode(loc, stream, triple.getS(), triple, graphRefAttr, nodeRefType, nodeRef, emitCtxt);
      auto edgeSetType = memberManager.getType(nodeRefType.getOutgoingMembers().getMembers()[0]);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "outgoing");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getOutgoingMembers().getMembers()[0], edgesDef}}));
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRefColumnRef;
      stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
      llvm::DenseSet<mlir::Value> joinedMultiMaps;
      stream = lowerPredicate(loc, stream, graphRefAttr, triple.getP(), edgeRefColumnRef, columnManager, triple, emitCtxt, joinedMultiMaps);
      stream = lowerTerm(loc, stream, triple.getO(), edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, triple, emitCtxt, joinedMultiMaps);
      ensureJointHashIndex(loc, stream, triple, emitCtxt);
      return stream;
   }
   mlir::Value lowerObjectFirst(mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt, bool restart) const {
      auto graphRefAttr = triple.getGraphRef();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      GraphDataOverrideGuard graphGuard(emitCtxt.graphs, graphRefAttr.getName());
      if (restart) {
         NamedGraphData fresh;
         stream = scanNamedGraph(rewriter, loc, graphRefAttr, emitCtxt.graphs, true, &fresh);
         graphGuard.activate(fresh);
      }
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      stream = resolveAnchorNode(loc, stream, triple.getO(), triple, graphRefAttr, nodeRefType, nodeRef, emitCtxt);
      auto edgeSetType = memberManager.getType(nodeRefType.getIncomingMembers().getMembers()[0]);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", "incoming");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getIncomingMembers().getMembers()[0], edgesDef}}));
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRefColumnRef;
      stream = scanEdges(loc, stream, edgesRef, edgeSetType, group, graph, edgeRefType, edgeRefColumnRef);
      llvm::DenseSet<mlir::Value> joinedMultiMaps;
      stream = lowerPredicate(loc, stream, graphRefAttr, triple.getP(), edgeRefColumnRef, columnManager, triple, emitCtxt, joinedMultiMaps);
      stream = lowerTerm(loc, stream, triple.getS(), edgeRefType.getFromMembers().getMembers()[0], edgeRefColumnRef, graph, triple, emitCtxt, joinedMultiMaps);
      ensureJointHashIndex(loc, stream, triple, emitCtxt);
      return stream;
   }
   mlir::Value lowerPredicateFirst(mlir::Location loc, mlir::Value stream, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt, bool restart) const {
      auto graphRefAttr = triple.getGraphRef();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      GraphDataOverrideGuard graphGuard(emitCtxt.graphs, graphRefAttr.getName());
      if (restart) {
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
      llvm::DenseSet<mlir::Value> joinedMultiMaps;
      stream = lowerPredicate(loc, stream, graphRefAttr, triple.getP(), edgeRefColumnRef, columnManager, triple, emitCtxt, joinedMultiMaps);
      stream = lowerTerm(loc, stream, triple.getS(), edgeRefType.getFromMembers().getMembers()[0], edgeRefColumnRef, graph, triple, emitCtxt, joinedMultiMaps);
      stream = lowerTerm(loc, stream, triple.getO(), edgeRefType.getToMembers().getMembers()[0], edgeRefColumnRef, graph, triple, emitCtxt, joinedMultiMaps);
      ensureJointHashIndex(loc, stream, triple, emitCtxt);
      return stream;
   }
   mlir::Value lowerPredicate(mlir::Location loc, mlir::Value stream, tuples::ColumnRefAttr graphRefAttr, mlir::Attribute p, tuples::ColumnRefAttr edgeRef, tuples::ColumnManager& columnManager, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt, llvm::DenseSet<mlir::Value>& joinedMultiMaps) const {
      auto graphRef = graphRefAttr.getName();
      auto [group, graph] = splitGraphRef(graphRefAttr);
      if (auto constPred = mlir::dyn_cast<gpm::IdentifierTermAttr>(p)) {
         auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constPred.getIdent());
         stream = rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, ident);
      }
      else if (auto varPred = mlir::dyn_cast<gpm::VariableTermAttr>(p)) {
         if (varPred.hasBinding() && triple.getJoinStrategy() != "hash") {
            auto& target = emitCtxt.bindings[varPred.getBindingReference().getName()];
            auto unwrapped =  unwrapNullableBinding(loc, stream, target.def);
            return filterMatchingIdentifiers(loc, unwrapped.stream, unwrapped.ref, edgeRef, subop::FilterSemantic::all_true);
         }
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
            mlir::Value inner = generateTupleStream(rewriter, loc, scanIdentDef, [&](mlir::OpBuilder&) -> mlir::Value {
               return identArg;
            });
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
               auto& binding = emitCtxt.bindings[ref.getName()];
               binding.def = def;
               binding.originTriple = triple.getOperation();
               inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), def);
            }
            rewriter.create<tuples::ReturnOp>(loc, inner);
         }
         stream = nestedMapOp.getRes();
         if (varPred.hasBinding()) {
            auto& target = emitCtxt.bindings[varPred.getBindingReference().getName()];
            stream = probeOrFilter(loc, stream, predNodeRef, target, nodesRef, graphData.nodeSetColumn->type, joinedMultiMaps);
         }
      }
      return stream;
   }
   mlir::Value lowerTerm(mlir::Location loc, mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::string graph, gpm::TriplePatternOp triple, TripleEmitContext& emitCtxt, llvm::DenseSet<mlir::Value>& joinedMultiMaps) const {
      auto ctxt = rewriter.getContext();
      auto& memberManager = ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
      auto joinStrategy = triple.getJoinStrategy();
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
            auto& target = emitCtxt.bindings[varTerm.getBindingReference().getName()];
            if (joinStrategy != "hash") {
               auto unwrapped = unwrapNullableBinding(loc, stream, target.def);
               stream = filterMatchingIdentifiers(loc, unwrapped.stream, unwrapped.ref, nodeRefColumnRef, subop::FilterSemantic::all_true);
            }
            else {
               auto candidateNodeRefType = mlir::cast<gsubop::NodeRefType>(memberManager.getType(nodeMember));
               auto idMember = candidateNodeRefType.getNodeMembers().getMembers()[0];
               auto [candidateIdDef, candidateIdRef] = createColumn(memberManager.getType(idMember), "hj", "candidate");
               stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRefColumnRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, candidateIdDef}}));
               auto& graphData = emitCtxt.graphs[triple.getGraphRef().getName()];
               stream = probeOrFilter(loc, stream, candidateIdRef, target, columnManager.createRef(graphData.nodeSetColumn), graphData.nodeSetColumn->type, joinedMultiMaps);
            }
         }
         else {
            auto ref = varTerm.getProducedBinding();
            auto def = createDef(columnManager, ref.getName().getRootReference().str(), ref.getName().getLeafReference().str(), memberManager.getType(nodeMember), false);
            auto& binding = emitCtxt.bindings[ref.getName()];
            binding.def = def;
            binding.originTriple = triple.getOperation();
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
         }
      }
      else if (auto bnode = mlir::dyn_cast<gpm::BNodeTermAttr>(term)) {
         auto name = resolveBindingName(term, triple);
         bool alreadyBound = name && emitCtxt.bindings.count(*name) > 0;
         if (alreadyBound) {
            auto [nodeRefColumnDef, nodeRefColumnRef] = createColumn(memberManager.getType(nodeMember), "nodes", "ref");
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, nodeRefColumnDef}}));
            auto& target = emitCtxt.bindings[*name];
            if (joinStrategy != "hash") {
               auto bindingRef = columnManager.createRef(target.def.getColumnPtr().get());
               stream = filterMatchingIdentifiers(loc, stream, bindingRef, nodeRefColumnRef, subop::FilterSemantic::all_true);
            }
            else {
               auto candidateNodeRefType = mlir::cast<gsubop::NodeRefType>(memberManager.getType(nodeMember));
               auto idMember = candidateNodeRefType.getNodeMembers().getMembers()[0];
               auto [candidateIdDef, candidateIdRef] = createColumn(memberManager.getType(idMember), "hj", "candidate");
               stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRefColumnRef, createColumnDefMemberMappingAttr(ctxt, {{idMember, candidateIdDef}}));
               auto& graphData = emitCtxt.graphs[triple.getGraphRef().getName()];
               stream = probeOrFilter(loc, stream, candidateIdRef, target, columnManager.createRef(graphData.nodeSetColumn), graphData.nodeSetColumn->type, joinedMultiMaps);
            }
         }
         else {
            auto def = createDef(columnManager, name->getRootReference().str(), name->getLeafReference().str(), memberManager.getType(nodeMember), false);
            auto& binding = emitCtxt.bindings[*name];
            binding.def = def;
            binding.originTriple = triple.getOperation();
            stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
         }
      }
      return stream;
   }
};

class TriplePatternLowering : public OpConversionPattern<gpm::TriplePatternOp> {
   NamedGraphMapping& graphs;
   VariableBinding& bindings;
   const llvm::DenseSet<mlir::SymbolRefAttr>& probedNames;
   public:
   TriplePatternLowering(TypeConverter& typeConverter, MLIRContext* context, NamedGraphMapping& graphs, VariableBinding& bindings, const llvm::DenseSet<mlir::SymbolRefAttr>& probedNames)
      : OpConversionPattern<gpm::TriplePatternOp>(typeConverter, context), graphs(graphs), bindings(bindings), probedNames(probedNames) {}
   LogicalResult matchAndRewrite(gpm::TriplePatternOp triple, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.setInsertionPoint(triple);
      TripleEmitContext emitCtxt{graphs, bindings, probedNames};
      TriplePatternEmitter emitter(rewriter);
      mlir::Value result = emitter.lowerTriple(triple->getLoc(), adaptor.getRel(), triple, emitCtxt);
      rewriter.replaceOp(triple, result);
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

   llvm::DenseSet<mlir::SymbolRefAttr> probedNames;
   collectProbedNames(module, probedNames);

   patterns.insert<NamedGraphLowering>(typeConverter, ctxt, graphs);
   patterns.insert<TriplePatternLowering>(typeConverter, ctxt, graphs, bindings, probedNames);

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
   pm.addPass(gpm::createPrepareRelAlgLoweringPass());
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
