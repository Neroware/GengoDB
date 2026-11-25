#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "gengodb/compiler/Conversion/GPMToSubOp/PathLowering.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/Variant/VariantOps.h"
#include "gengodb/semantics/Datatypes.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/SubOperator/Utils.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

// Lowering of gpm.relalg.path (SPARQL `s (p1|...|pn)+ o` / `s (p1|...|pn)* o`): the leaf relation
// { (x, y) | y reachable from x over >= min step edges }, joined with the rest of the plan like a triple.
//
//   Phase 1  seeds -> %anchors (distinct i32 node ids) -> %frontier0 = {(a, a)}  (p*: visited ∪= {(a, a)})
//              constant end:  that node; the search runs backwards if only the object is constant
//              both variables: p+ the sources of all step edges, p* nodes(G) (endpoints of all edges)
//   Phase 2  subop.loop over the frontier: one step (s, n) -p-> (s, m) per iteration,
//            Δ = step − visited, visited ∪= Δ, continue while Δ ≠ ∅
//   Phase 3  output = %visited, filtered for a constant other end / the same variable on both ends,
//            the node ids wrapped into variants; p* with a constant end not in G adds (c, c)
//
// Inside the loop everything is keyed by the graph's local i32 node ids. The loop body may write
// %visited (a hashmap created outside the loop) but no multimap: SpecializeSubOp would place the
// multimap's hash-indexed view inside the loop.

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;
using namespace helpers;

namespace {
using Member = subop::Member;

bool isRelAlgOperator(mlir::Operation* op) {
   return op && !mlir::isa<gpm::GPMOperator>(op) && mlir::isa<Operator>(op);
}

// eq region over `numKeys` i32 keys: (l1..ln, r1..rn) -> and_i (li == ri)
mlir::Block* createI32EqBlock(mlir::OpBuilder& builder, mlir::Location loc, unsigned numKeys) {
   auto* block = new mlir::Block;
   auto i32 = builder.getI32Type();
   for (unsigned i = 0; i < 2 * numKeys; ++i) block->addArgument(i32, loc);
   mlir::OpBuilder::InsertionGuard guard(builder);
   builder.setInsertionPointToStart(block);
   mlir::Value result;
   for (unsigned i = 0; i < numKeys; ++i) {
      mlir::Value eq = builder.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, block->getArgument(i), block->getArgument(numKeys + i));
      result = result ? builder.create<mlir::arith::AndIOp>(loc, result, eq).getResult() : eq;
   }
   builder.create<tuples::ReturnOp>(loc, result);
   return block;
}
// init region returning the given constants (none for a pure set)
mlir::Block* createInitBlock(mlir::OpBuilder& builder, mlir::Location loc, llvm::ArrayRef<std::pair<mlir::Type, int64_t>> values) {
   auto* block = new mlir::Block;
   mlir::OpBuilder::InsertionGuard guard(builder);
   builder.setInsertionPointToStart(block);
   llvm::SmallVector<mlir::Value> results;
   for (auto [type, value] : values) {
      results.push_back(builder.create<mlir::arith::ConstantIntOp>(loc, value, type));
   }
   builder.create<tuples::ReturnOp>(loc, results);
   return block;
}

// GraphEmitter over a private NamedGraphMapper: every phase works on its own scan of the graph state (a
// state stream such as the frontier doesn't carry the graph's set columns), and the inherited helpers
// use the set columns of the most recent scan.
class PathEmitter : public GraphEmitter {
   public:
   PathEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& localGraphs, mlir::Operation* op, tuples::ColumnRefAttr graphRef)
      : GraphEmitter(rewriter, localGraphs, op, graphRef) {}

   const NamedGraph& current() { return graphs[graphName]; }
   void setCurrent(const NamedGraph& namedGraph) { graphs[graphName] = namedGraph; }

   // one-tuple stream holding the graph's node/edge set in fresh columns
   mlir::Value scanGraph() {
      NamedGraph& namedGraph = graphs[graphName];
      auto nodeSetDef = createDef(columnManager, group, graph + "_vx", namedGraph.nodeSetColumn->type);
      auto edgeSetDef = createDef(columnManager, group, graph + "_ex", namedGraph.edgeSetColumn->type);
      namedGraph.nodeSetColumn = &nodeSetDef.getColumn();
      namedGraph.edgeSetColumn = &edgeSetDef.getColumn();
      return rewriter.create<gsubop::ScanGraphOp>(loc, namedGraph.externalGraph, nodeSetDef, edgeSetDef);
   }

   // nested_map over the graph tuple: attaches `buildInner()`'s stream to the node/edge set columns
   mlir::Value attachToGraph(mlir::Value graphStream, llvm::function_ref<mlir::Value()> buildInner) {
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), graphStream, rewriter.getArrayAttr({columnManager.createRef(current().nodeSetColumn)}));
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      block->addArgument(current().nodeSetColumn->type, loc);
      nestedMapOp.getRegion().push_back(block);
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(block);
      rewriter.create<tuples::ReturnOp>(loc, buildInner());
      return nestedMapOp.getRes();
   }

   gsubop::CreateIdentifierOp createIdentifier(mlir::OpBuilder& builder, gpm::IdentifierTermAttr ident, std::optional<size_t> paramId) {
      auto identOp = builder.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, ident.getIdent());
      if (paramId) identOp->setAttr(relalg::kQueryParamsAttrName, relalg::makeQueryParamsAttr(ctxt, *paramId, ident.getIdent()));
      return identOp;
   }

   // constant term -> i32 node id column; the stream is empty if the term isn't a node of G
   mlir::Value constantNodeId(mlir::Value stream, gpm::IdentifierTermAttr ident, std::optional<size_t> paramId, tuples::ColumnRefAttr& idRef) {
      auto nodeRefType = createNodeRefType(ctxt, group, graph);
      auto [nodeDef, nodeRef] = createColumn(nodeRefType, "nodes", "ref");
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({columnManager.createRef(current().nodeSetColumn)}));
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      auto nodeSetArg = block->addArgument(current().nodeSetColumn->type, loc);
      nestedMapOp.getRegion().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
         auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& b) -> mlir::Value { return createIdentifier(b, ident, paramId); });
         scan = filterByIdentifier(scan, identRef);
         mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({identRef}), nodeDef);
         rewriter.create<tuples::ReturnOp>(loc, lookup);
      }
      return nodeIds(nestedMapOp.getRes(), nodeRef, nodeRefType, idRef);
   }

   // one tuple per edge -> i32 node id of its source (or target); only edges whose predicate is one of
   // `predicates`, if any are given. The sources and targets of all edges are nodes(G), the subjects and
   // objects of G's triples (the graph's node set also holds vocabulary nodes, e.g. predicates and datatypes).
   mlir::Value edgeEndpointIds(mlir::Value stream, bool source, llvm::ArrayRef<mlir::Attribute> predicates, tuples::ColumnRefAttr& idRef) {
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRef;
      stream = scanEdges(stream, edgeRefType, edgeRef);
      if (!predicates.empty()) stream = filterByIdentifiers(stream, edgeRef, predicates);
      auto endpointMember = source ? edgeRefType.getFromMembers().getMembers()[0] : edgeRefType.getToMembers().getMembers()[0];
      tuples::ColumnRefAttr rawRef;
      stream = emitEdgeToNode(stream, edgeRef, endpointMember, rawRef);
      return nodeIds(stream, rawRef, mlir::cast<gsubop::NodeRefType>(memberManager.getType(endpointMember)), idRef);
   }

   // i32 node id -> node ref (the id must be valid in G)
   mlir::Value lookupNode(mlir::Value stream, tuples::ColumnRefAttr idRef, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef) {
      nodeRefType = createNodeRefType(ctxt, group, graph);
      auto [nodeDef, ref] = createColumn(nodeRefType, "nodes", "ref");
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({columnManager.createRef(current().nodeSetColumn), idRef}));
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      auto nodeSetArg = block->addArgument(current().nodeSetColumn->type, loc);
      auto idArg = block->addArgument(rewriter.getI32Type(), loc);
      nestedMapOp.getRegion().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         auto [keyDef, keyRef] = createColumn(rewriter.getI32Type(), "nodes", "lookup");
         auto scan = generateTupleStream(rewriter, loc, keyDef, [&](mlir::OpBuilder&) -> mlir::Value { return idArg; });
         mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({keyRef}), nodeDef);
         rewriter.create<tuples::ReturnOp>(loc, lookup);
      }
      nodeRef = ref;
      return nestedMapOp.getRes();
   }

   // node ref -> its i32 node id
   mlir::Value nodeIds(mlir::Value stream, tuples::ColumnRefAttr nodeRef, gsubop::NodeRefType nodeRefType, tuples::ColumnRefAttr& idRef) {
      auto [idDef, ref] = createColumn(rewriter.getI32Type(), "path", "id");
      idRef = ref;
      return rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getNodeMembers().getMembers()[0], idDef}}));
   }

   // one step from node id `fromRef` along the step edges: -> i32 id of the neighbor
   mlir::Value step(mlir::Value stream, tuples::ColumnRefAttr fromRef, bool outgoing, llvm::ArrayRef<mlir::Attribute> predicates, tuples::ColumnRefAttr& toRef) {
      gsubop::NodeRefType nodeRefType;
      tuples::ColumnRefAttr nodeRef;
      stream = lookupNode(stream, fromRef, nodeRefType, nodeRef);
      auto edgeMember = outgoing ? nodeRefType.getOutgoingMembers().getMembers()[0] : nodeRefType.getIncomingMembers().getMembers()[0];
      auto edgeSetType = memberManager.getType(edgeMember);
      auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", outgoing ? "outgoing" : "incoming");
      stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{edgeMember, edgesDef}}));
      gsubop::EdgeRefType edgeRefType;
      tuples::ColumnRefAttr edgeRef;
      stream = scanEdges(stream, edgesRef, edgeSetType, edgeRefType, edgeRef);
      stream = filterByIdentifiers(stream, edgeRef, predicates);
      auto endpointMember = outgoing ? edgeRefType.getToMembers().getMembers()[0] : edgeRefType.getFromMembers().getMembers()[0];
      tuples::ColumnRefAttr rawRef;
      stream = emitEdgeToNode(stream, edgeRef, endpointMember, rawRef);
      return nodeIds(stream, rawRef, mlir::cast<gsubop::NodeRefType>(memberManager.getType(endpointMember)), toRef);
   }

   // a single tuple iff the constant is NOT a node of G (p*: such a constant still reaches itself);
   // `valueRef` = the constant as an IRI variant, as the SPARQL translator builds IRI constants
   mlir::Value constantNotInGraph(mlir::Value stream, gpm::IdentifierTermAttr ident, std::optional<size_t> paramId, tuples::ColumnRefAttr& valueRef) {
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({columnManager.createRef(current().nodeSetColumn)}));
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      block->addArgument(current().nodeSetColumn->type, loc);
      nestedMapOp.getRegion().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         auto [keyDef, keyRef] = createColumn(gsubop::IdentifierType::get(ctxt), "path", "outside_key");
         auto scan = generateTupleStream(rewriter, loc, keyDef, [&](mlir::OpBuilder& b) -> mlir::Value { return createIdentifier(b, ident, paramId); });
         auto [outsideDef, outsideRef] = createColumn(rewriter.getI1Type(), "path", "outside");
         subop::MapCreationHelper helper(ctxt);
         helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
            mlir::Value valid = b.create<gsubop::IdentifierValidOp>(loc, b.getI1Type(), helper.access(keyRef, loc));
            mlir::Value one = b.create<mlir::arith::ConstantIntOp>(loc, 1, b.getI1Type());
            b.create<tuples::ReturnOp>(loc, mlir::Value(b.create<mlir::arith::XOrIOp>(loc, valid, one)));
         });
         auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), scan, rewriter.getArrayAttr({outsideDef}), helper.getColRefs());
         mapOp.getFn().push_back(helper.getMapBlock());
         mlir::Value filtered = rewriter.create<subop::FilterOp>(loc, mapOp.getResult(), subop::FilterSemantic::all_true, rewriter.getArrayAttr({outsideRef}));
         rewriter.create<tuples::ReturnOp>(loc, filtered);
      }
      auto [valueDef, ref] = createColumn(variant::VariantType::get(ctxt), "path", "constant_value");
      subop::MapCreationHelper helper(ctxt);
      helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
         mlir::Value str = b.create<db::ConstantOp>(loc, db::StringType::get(ctxt), b.getStringAttr(ident.identifier()));
         auto typeId = b.getI32IntegerAttr(gengodb::semantics::xsd::to_int32(gengodb::semantics::xsd::Type::AnyIRI));
         b.create<tuples::ReturnOp>(loc, mlir::Value(b.create<variant::CreateScalarOp>(loc, variant::VariantType::get(ctxt), str, typeId)));
      });
      auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), nestedMapOp.getRes(), rewriter.getArrayAttr({valueDef}), helper.getColRefs());
      mapOp.getFn().push_back(helper.getMapBlock());
      valueRef = ref;
      return mapOp.getResult();
   }

   // keeps tuples with left = right (i32 node ids)
   mlir::Value filterIdsEqual(mlir::Value stream, tuples::ColumnRefAttr left, tuples::ColumnRefAttr right) {
      auto [eqDef, eqRef] = createColumn(rewriter.getI1Type(), "path", "same");
      subop::MapCreationHelper helper(ctxt);
      helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
         mlir::Value eq = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, helper.access(left, loc), helper.access(right, loc));
         b.create<tuples::ReturnOp>(loc, eq);
      });
      auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({eqDef}), helper.getColRefs());
      mapOp.getFn().push_back(helper.getMapBlock());
      return rewriter.create<subop::FilterOp>(loc, mapOp.getResult(), subop::FilterSemantic::all_true, rewriter.getArrayAttr({eqRef}));
   }

   // keep edges whose predicate is one of `predicates`
   mlir::Value filterByIdentifiers(mlir::Value stream, tuples::ColumnRefAttr edgeRef, llvm::ArrayRef<mlir::Attribute> predicates) {
      llvm::SmallVector<mlir::Value> idents;
      for (auto predicate : predicates) {
         idents.push_back(rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, mlir::cast<gpm::IdentifierTermAttr>(predicate).getIdent()));
      }
      return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, idents);
   }
};

class PathLowering : public mlir::OpConversionPattern<gpm::PathOp> {
   NamedGraphMapper& graphs;

   public:
   PathLowering(mlir::TypeConverter& typeConverter, mlir::MLIRContext* context, NamedGraphMapper& graphs)
      : mlir::OpConversionPattern<gpm::PathOp>(typeConverter, context), graphs(graphs) {}

   mlir::LogicalResult matchAndRewrite(gpm::PathOp pathOp, OpAdaptor adaptor, mlir::ConversionPatternRewriter& rewriter) const override {
      auto* ctxt = rewriter.getContext();
      auto loc = pathOp.getLoc();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto tupleStreamType = tuples::TupleStreamType::get(ctxt);
      auto i1 = rewriter.getI1Type();
      auto i32 = rewriter.getI32Type();

      // ends: identifiers or column definitions (gpm-unnest-patterns turned every reference into a def)
      const tuples::Column* ends[2] = {nullptr, nullptr};
      gpm::IdentifierTermAttr idents[2];
      for (auto [i, term] : llvm::enumerate(std::array<mlir::Attribute, 2>{pathOp.getS(), pathOp.getO()})) {
         if (auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
            idents[i] = ident;
            continue;
         }
         auto varTerm = mlir::cast<gpm::VariableTermAttr>(term);
         if (!varTerm.getProducedBinding())
            return pathOp.emitOpError("path is not normalized (expected only identifiers and column defs); run gpm-unnest-patterns first");
         ends[i] = &varTerm.getProducedBinding().getColumn();
         if (!mlir::isa<variant::VariantType>(ends[i]->type))
            return pathOp.emitOpError("path end columns must be variants (resolved by gpm-unnest-patterns)");
      }
      auto graphIt = graphs.find(pathOp.getGraphRef().getName());
      if (graphIt == graphs.end())
         return pathOp.emitOpError("the path's named graph must be lowered before the path");
      const NamedGraph inputGraph = graphIt->second;

      // search from the constant end, backwards if only the object is constant; with two variable ends
      // forwards from every possible start
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
      PathEmitter emitter(rewriter, localGraphs, pathOp, pathOp.getGraphRef());

      // ───── Phase 1: seeds -> distinct anchor ids -> frontier0 = {(a, a)} ─────
      llvm::SmallVector<std::pair<mlir::Value, tuples::ColumnRefAttr>> seeds;
      auto addSeeds = [&](auto buildSeeds) {
         tuples::ColumnRefAttr idRef;
         mlir::Value stream = buildSeeds(emitter.scanGraph(), idRef);
         seeds.push_back({stream, idRef});
      };
      if (anchorIdent) {
         addSeeds([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { return emitter.constantNodeId(graphStream, anchorIdent, anchorParam, idRef); });
      } else if (zeroLength) {
         // p*: every node of G reaches itself, so start from all of nodes(G) = sources ∪ targets of all edges
         addSeeds([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { return emitter.edgeEndpointIds(graphStream, /*source=*/true, {}, idRef); });
         addSeeds([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { return emitter.edgeEndpointIds(graphStream, /*source=*/false, {}, idRef); });
      } else {
         // p+: a path starts with a step edge
         addSeeds([&](mlir::Value graphStream, tuples::ColumnRefAttr& idRef) { return emitter.edgeEndpointIds(graphStream, /*source=*/true, predicates, idRef); });
      }
      auto anchorKey = createMember(ctxt, "path_anchor", i32);
      auto anchorsType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {anchorKey}), createStateMembersAttr(ctxt, {}), false);
      mlir::Value anchors = rewriter.create<subop::GenericCreateOp>(loc, anchorsType);
      for (auto [seedStream, seedIdRef] : seeds) {
         auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, anchorsType), "path", "anchorref");
         auto insertAnchors = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, seedStream, anchors, rewriter.getArrayAttr({seedIdRef}), refDef);
         insertAnchors.getEqFn().push_back(createI32EqBlock(rewriter, loc, 1));
         insertAnchors.getInitFn().push_back(createInitBlock(rewriter, loc, {}));
      }
      auto f0Start = createMember(ctxt, "path_f0_start", i32);
      auto f0Node = createMember(ctxt, "path_f0_node", i32);
      auto frontier0Type = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, {f0Start, f0Node}));
      mlir::Value frontier0 = rewriter.create<subop::GenericCreateOp>(loc, frontier0Type);
      {
         auto [idDef, idRef] = createColumn(i32, "path", "anchor_id");
         mlir::Value scan = rewriter.create<subop::ScanOp>(loc, anchors, createColumnDefMemberMappingAttr(ctxt, {{anchorKey, idDef}}));
         rewriter.create<subop::MaterializeOp>(loc, scan, frontier0, subop::ColumnRefMemberMappingAttr::get(ctxt, {{f0Start, idRef}, {f0Node, idRef}}));
      }

      auto visitedStart = createMember(ctxt, "path_v_start", i32);
      auto visitedNode = createMember(ctxt, "path_v_node", i32);
      auto visitedSeen = createMember(ctxt, "path_v_seen", i1);
      auto visitedType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {visitedStart, visitedNode}), createStateMembersAttr(ctxt, {visitedSeen}), false);
      mlir::Value visited = rewriter.create<subop::GenericCreateOp>(loc, visitedType);
      if (zeroLength) {
         // p*: every anchor reaches itself; seen = true so the loop never re-emits (a, a)
         auto [idDef, idRef] = createColumn(i32, "path", "anchor_self");
         mlir::Value scan = rewriter.create<subop::ScanOp>(loc, anchors, createColumnDefMemberMappingAttr(ctxt, {{anchorKey, idDef}}));
         auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, visitedType), "path", "selfref");
         auto seed = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, scan, visited, rewriter.getArrayAttr({idRef, idRef}), refDef);
         seed.getEqFn().push_back(createI32EqBlock(rewriter, loc, 2));
         seed.getInitFn().push_back(createInitBlock(rewriter, loc, {{i1, 1}}));
      }

      // ───── Phase 2: fixpoint ─────
      auto fStart = createMember(ctxt, "path_f_start", i32);
      auto fNode = createMember(ctxt, "path_f_node", i32);
      auto frontierType = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, {fStart, fNode}));
      auto loopOp = rewriter.create<subop::LoopOp>(loc, mlir::TypeRange{frontierType}, mlir::ValueRange{frontier0});
      auto* body = new mlir::Block;
      mlir::Value frontier = body->addArgument(frontierType, loc);
      loopOp.getBodyRegion().push_back(body);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(body);
         auto nStart = createMember(ctxt, "path_n_start", i32);
         auto nNode = createMember(ctxt, "path_n_node", i32);
         auto nextType = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, {nStart, nNode}));
         mlir::Value next = rewriter.create<subop::GenericCreateOp>(loc, nextType);
         auto cStart = createMember(ctxt, "path_c_start", i32);
         auto cNode = createMember(ctxt, "path_c_node", i32);
         auto candType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {cStart, cNode}), createStateMembersAttr(ctxt, {}), false);
         mlir::Value cand = rewriter.create<subop::GenericCreateOp>(loc, candType);
         auto goMember = createMember(ctxt, "path_go", i1);
         auto goType = subop::SimpleStateType::get(ctxt, createStateMembersAttr(ctxt, {goMember}));
         auto goCreate = rewriter.create<subop::CreateSimpleStateOp>(loc, goType);
         goCreate.getInitFn().push_back(createInitBlock(rewriter, loc, {{i1, 0}}));
         mlir::Value go = goCreate.getRes();

         // 2a. step (s, n) -p-> (s, m), deduplicated within the iteration
         auto [sDef, sRef] = createColumn(i32, "path", "start");
         auto [nDef, nRef] = createColumn(i32, "path", "node");
         mlir::Value stepStream = emitter.attachToGraph(emitter.scanGraph(), [&]() -> mlir::Value {
            return rewriter.create<subop::ScanOp>(loc, frontier, createColumnDefMemberMappingAttr(ctxt, {{fStart, sDef}, {fNode, nDef}}));
         });
         tuples::ColumnRefAttr mRef;
         stepStream = emitter.step(stepStream, nRef, forward, predicates, mRef);
         {
            auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, candType), "path", "candref");
            auto insertCand = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, stepStream, cand, rewriter.getArrayAttr({sRef, mRef}), refDef);
            insertCand.getEqFn().push_back(createI32EqBlock(rewriter, loc, 2));
            insertCand.getInitFn().push_back(createInitBlock(rewriter, loc, {}));
         }

         // 2b. Δ = cand − visited ; visited ∪= Δ ; next = Δ
         auto [dsDef, dsRef] = createColumn(i32, "path", "delta_start");
         auto [dnDef, dnRef] = createColumn(i32, "path", "delta_node");
         mlir::Value delta = rewriter.create<subop::ScanOp>(loc, cand, createColumnDefMemberMappingAttr(ctxt, {{cStart, dsDef}, {cNode, dnDef}}));
         auto [visRefDef, visRef] = createColumn(subop::LookupEntryRefType::get(ctxt, visitedType), "path", "visref");
         auto lookupVisited = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, delta, visited, rewriter.getArrayAttr({dsRef, dnRef}), visRefDef);
         lookupVisited.getEqFn().push_back(createI32EqBlock(rewriter, loc, 2));
         lookupVisited.getInitFn().push_back(createInitBlock(rewriter, loc, {{i1, 0}}));
         auto [seenDef, seenRef] = createColumn(i1, "path", "seen");
         delta = rewriter.create<subop::GatherOp>(loc, lookupVisited.getRes(), visRef, createColumnDefMemberMappingAttr(ctxt, {{visitedSeen, seenDef}}));
         delta = rewriter.create<subop::FilterOp>(loc, delta, subop::FilterSemantic::none_true, rewriter.getArrayAttr({seenRef}));
         auto [trueDef, trueRef] = createColumn(i1, "path", "true");
         {
            subop::MapCreationHelper helper(ctxt);
            helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
               mlir::Value trueValue = b.create<mlir::arith::ConstantIntOp>(loc, 1, i1);
               b.create<tuples::ReturnOp>(loc, trueValue);
            });
            auto mapOp = rewriter.create<subop::MapOp>(loc, tupleStreamType, delta, rewriter.getArrayAttr({trueDef}), helper.getColRefs());
            mapOp.getFn().push_back(helper.getMapBlock());
            delta = mapOp.getResult();
         }
         rewriter.create<subop::ScatterOp>(loc, delta, visRef, subop::ColumnRefMemberMappingAttr::get(ctxt, {{visitedSeen, trueRef}}));
         rewriter.create<subop::MaterializeOp>(loc, delta, next, subop::ColumnRefMemberMappingAttr::get(ctxt, {{nStart, dsRef}, {nNode, dnRef}}));

         // 2c. continue while Δ ≠ ∅
         auto [goRefDef, goRef] = createColumn(subop::EntryRefType::get(ctxt, goType), "path", "goref");
         mlir::Value goStream = rewriter.create<subop::LookupOp>(loc, tupleStreamType, delta, go, rewriter.getArrayAttr({}), goRefDef);
         rewriter.create<subop::ScatterOp>(loc, goStream, goRef, subop::ColumnRefMemberMappingAttr::get(ctxt, {{goMember, trueRef}}));
         rewriter.create<subop::LoopContinueOp>(loc, go, subop::MemberAttr::get(ctxt, goMember), mlir::ValueRange{next});
      }

      // ───── Phase 3: the pairs in %visited, on the input graph stream ─────
      emitter.setCurrent(inputGraph);
      auto [vsDef, vsRef] = createColumn(i32, "path", "visited_start");
      auto [vnDef, vnRef] = createColumn(i32, "path", "visited_node");
      mlir::Value out = emitter.attachToGraph(adaptor.getRel(), [&]() -> mlir::Value {
         return rewriter.create<subop::ScanOp>(loc, visited, createColumnDefMemberMappingAttr(ctxt, {{visitedStart, vsDef}, {visitedNode, vnDef}}));
      });
      if (otherIdent) {
         // both ends constant: keep the pair iff it ends at the object
         tuples::ColumnRefAttr otherIdRef;
         out = emitter.constantNodeId(out, otherIdent, otherParam, otherIdRef);
         out = emitter.filterIdsEqual(out, vnRef, otherIdRef);
      }
      if (sameVariable) out = emitter.filterIdsEqual(out, vsRef, vnRef);
      auto wrapEnd = [&](tuples::ColumnRefAttr idRef, const tuples::Column* column) {
         gsubop::NodeRefType nodeRefType;
         tuples::ColumnRefAttr nodeRef;
         out = emitter.lookupNode(out, idRef, nodeRefType, nodeRef);
         // re-created without `fromExisting`: the op itself just defines the column
         out = emitter.wrapNode(out, nodeRef, columnManager.createDef(column));
      };
      if (ends[0]) wrapEnd(forward ? vsRef : vnRef, ends[0]);
      if (ends[1] && !sameVariable) wrapEnd(forward ? vnRef : vsRef, ends[1]);

      // ───── p*: a constant end that isn't a node of G still reaches itself (zero-length path) ─────
      if (zeroLength && anchorIdent && (!otherIdent || otherIdent == anchorIdent)) {
         tuples::ColumnRefAttr valueRef;
         mlir::Value outside = emitter.constantNotInGraph(emitter.scanGraph(), anchorIdent, anchorParam, valueRef);
         if (const auto* otherEnd = forward ? ends[1] : ends[0]) {
            subop::MapCreationHelper helper(ctxt);
            helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) { b.create<tuples::ReturnOp>(loc, helper.access(valueRef, loc)); });
            auto mapOp = rewriter.create<subop::MapOp>(loc, tupleStreamType, outside, rewriter.getArrayAttr({columnManager.createDef(otherEnd)}), helper.getColRefs());
            mapOp.getFn().push_back(helper.getMapBlock());
            outside = mapOp.getResult();
         }
         out = rewriter.create<subop::UnionOp>(loc, tupleStreamType, mlir::ValueRange{out, outside});
      }

      // relalg consumers need the stream re-typed as a relalg operator (cf. replaceTripleOp)
      bool needsInFlight = llvm::any_of(pathOp.getRes().getUses(), [](mlir::OpOperand& use) { return isRelAlgOperator(use.getOwner()); });
      if (needsInFlight) {
         relalg::AvailabilityCache cache;
         auto available = mlir::cast<Operator>(pathOp.getOperation()).getAvailableColumns(cache).asRefArrayAttr(ctxt);
         auto inFlight = rewriter.create<relalg::InFlightOp>(loc, out, available);
         rewriter.replaceUsesWithIf(pathOp.getRes(), inFlight.getRes(), [](mlir::OpOperand& use) { return isRelAlgOperator(use.getOwner()); });
      }
      rewriter.replaceOp(pathOp, out);
      return mlir::success();
   }
};
} // namespace

void populatePathLoweringPatterns(mlir::RewritePatternSet& patterns, mlir::TypeConverter& typeConverter, NamedGraphMapper& graphs) {
   patterns.insert<PathLowering>(typeConverter, patterns.getContext(), graphs);
}
} // namespace gengodb::compiler::dialect::gpm::lowering
