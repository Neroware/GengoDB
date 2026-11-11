#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "gengodb/compiler/Conversion/GPMToSubOp/PathExpansionLowering.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/Variant/VariantOps.h"
#include "gengodb/semantics/Datatypes.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/SubOperator/Utils.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

// Lowering of gpm.relalg.path_expansion (SPARQL p+ / p*) to a semi-naive multi-source BFS:
//
//   Phase 0  materialize R into a buffer (the op is a pipeline breaker)
//   Phase 1  distinct anchor node ids -> %frontier0 = {(a, a)}      (p*: also visited ∪= {(a, a)})
//   Phase 2  subop.loop over the frontier: one step (s, n) -p-> (s, m) per iteration,
//            Δ = step − visited, visited ∪= Δ, continue while Δ ≠ ∅
//   Phase 3  %reach (multimap start -> end) built from %visited, then R ⋈ reach
//
// Inside the loop everything is keyed by the graph's local i32 node ids; only the output is
// wrapped into a variant. The loop body may write %visited (a hashmap created outside the loop),
// but not a multimap: SpecializeSubOp would place the multimap's hash-indexed view inside the
// loop. That's why %reach is built after the loop.

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

// GraphEmitter over a private NamedGraphMapper: every phase scans the graph state anew (the
// frontier/R streams don't carry the graph's set columns), and the inherited helpers then work on
// the columns of the most recent scan.
class PathEmitter : public GraphEmitter {
   ExternalGraph& ext;

   public:
   PathEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& localGraphs, mlir::Operation* op, tuples::ColumnRefAttr graphRef, ExternalGraph& ext)
      : GraphEmitter(rewriter, localGraphs, op, graphRef), ext(ext) {}

   // One-tuple stream holding the graph's node/edge set. With `reuse`, it re-defines those
   // existing columns (so ops that were lowered against them find them in the stream).
   mlir::Value scanGraph(const NamedGraph* reuse = nullptr) {
      tuples::ColumnDefAttr nodeSetDef, edgeSetDef;
      if (reuse) {
         nodeSetDef = columnManager.createDef(reuse->nodeSetColumn);
         edgeSetDef = columnManager.createDef(reuse->edgeSetColumn);
      } else {
         nodeSetDef = createDef(columnManager, group, graph + "_vx", ext.nodeSetType);
         edgeSetDef = createDef(columnManager, group, graph + "_ex", ext.edgeSetType);
      }
      graphs[graphName] = NamedGraph{ext.graphType, &nodeSetDef.getColumn(), &edgeSetDef.getColumn(), ext.externalGraph};
      return rewriter.create<gsubop::ScanGraphOp>(loc, ext.externalGraph, nodeSetDef, edgeSetDef);
   }
   const NamedGraph& current() { return graphs[graphName]; }

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

   // anchor term -> i32 node id column in G; tuples whose anchor isn't a node of G are dropped
   mlir::Value anchorIds(mlir::Value stream, mlir::Attribute anchorTerm, std::optional<size_t> paramId, tuples::ColumnRefAttr& idRef) {
      auto nodeSetType = current().nodeSetColumn->type;
      if (auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(anchorTerm)) {
         auto nodeRefType = createNodeRefType(ctxt, group, graph);
         auto [nodeDef, nodeRef] = createColumn(nodeRefType, "nodes", "ref");
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({columnManager.createRef(current().nodeSetColumn)}));
         auto* block = new mlir::Block;
         block->addArgument(tuples::TupleType::get(ctxt), loc);
         auto nodeSetArg = block->addArgument(nodeSetType, loc);
         nestedMapOp.getRegion().push_back(block);
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(block);
            auto [identDef, identRef] = createColumn(gsubop::IdentifierType::get(ctxt), "idents", "lookup");
            auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& b) -> mlir::Value {
               auto identOp = b.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, ident.getIdent());
               if (paramId) relalg::forwardParameter(op, *paramId, identOp.getOperation());
               return identOp;
            });
            scan = filterByIdentifier(scan, identRef);
            mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({identRef}), nodeDef);
            rewriter.create<tuples::ReturnOp>(loc, lookup);
         }
         return nodeIds(nestedMapOp.getRes(), nodeRef, nodeRefType, idRef);
      }
      auto variantRef = mlir::cast<gpm::VariableTermAttr>(anchorTerm).getBindingReference();
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({columnManager.createRef(current().nodeSetColumn), variantRef}));
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      auto nodeSetArg = block->addArgument(nodeSetType, loc);
      auto variantArg = block->addArgument(variantRef.getColumn().type, loc);
      nestedMapOp.getRegion().push_back(block);
      auto [idDef, ref] = createColumn(rewriter.getI32Type(), "path", "id");
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         auto scan = generateTupleStream(rewriter, loc, idDef, [&](mlir::OpBuilder& b) -> mlir::Value {
            return b.create<variant::ResolveNodeOp>(loc, b.getI32Type(), nodeSetArg, variantArg);
         });
         rewriter.create<tuples::ReturnOp>(loc, scan);
      }
      idRef = ref;
      return filterByIdentifier(nestedMapOp.getRes(), idRef);
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

   // keeps the tuples whose anchor is NOT a node of G (p*: such an anchor still reaches itself);
   // `anchorValueRef` = the anchor as a variant
   mlir::Value anchorsNotInGraph(mlir::Value stream, mlir::Attribute anchorTerm, std::optional<size_t> paramId, tuples::ColumnRefAttr& anchorValueRef) {
      auto nodeSetType = current().nodeSetColumn->type;
      auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(anchorTerm);
      tuples::ColumnRefAttr variantRef;
      llvm::SmallVector<mlir::Attribute> params{columnManager.createRef(current().nodeSetColumn)};
      if (!ident) {
         variantRef = mlir::cast<gpm::VariableTermAttr>(anchorTerm).getBindingReference();
         params.push_back(variantRef);
      }
      auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr(params));
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      auto nodeSetArg = block->addArgument(nodeSetType, loc);
      mlir::Value variantArg = ident ? mlir::Value() : block->addArgument(variantRef.getColumn().type, loc);
      nestedMapOp.getRegion().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         mlir::Type keyType = ident ? mlir::Type(gsubop::IdentifierType::get(ctxt)) : mlir::Type(rewriter.getI32Type());
         auto [keyDef, keyRef] = createColumn(keyType, "path", "outside_key");
         auto scan = generateTupleStream(rewriter, loc, keyDef, [&](mlir::OpBuilder& b) -> mlir::Value {
            if (!ident) return b.create<variant::ResolveNodeOp>(loc, b.getI32Type(), nodeSetArg, variantArg);
            auto identOp = b.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, ident.getIdent());
            if (paramId) relalg::forwardParameter(op, *paramId, identOp.getOperation());
            return identOp;
         });
         auto [outsideDef, outsideRef] = createColumn(rewriter.getI1Type(), "path", "outside");
         subop::MapCreationHelper helper(ctxt);
         helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
            mlir::Value key = helper.access(keyRef, loc);
            mlir::Value valid;
            if (ident) {
               valid = b.create<gsubop::IdentifierValidOp>(loc, b.getI1Type(), key);
            } else {
               mlir::Value zero = b.create<mlir::arith::ConstantIntOp>(loc, 0, b.getI32Type());
               valid = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sge, key, zero);
            }
            mlir::Value one = b.create<mlir::arith::ConstantIntOp>(loc, 1, b.getI1Type());
            b.create<tuples::ReturnOp>(loc, mlir::Value(b.create<mlir::arith::XOrIOp>(loc, valid, one)));
         });
         auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), scan, rewriter.getArrayAttr({outsideDef}), helper.getColRefs());
         mapOp.getFn().push_back(helper.getMapBlock());
         mlir::Value filtered = rewriter.create<subop::FilterOp>(loc, mapOp.getResult(), subop::FilterSemantic::all_true, rewriter.getArrayAttr({outsideRef}));
         rewriter.create<tuples::ReturnOp>(loc, filtered);
      }
      if (!ident) {
         anchorValueRef = variantRef;
         return nestedMapOp.getRes();
      }
      // constant IRI not in G -> IRI variant, as the SPARQL translator builds IRI constants
      auto [valueDef, valueRef] = createColumn(variant::VariantType::get(ctxt), "path", "anchor_value");
      subop::MapCreationHelper helper(ctxt);
      helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
         mlir::Value str = b.create<db::ConstantOp>(loc, db::StringType::get(ctxt), b.getStringAttr(ident.identifier()));
         auto typeId = b.getI32IntegerAttr(gengodb::semantics::xsd::to_int32(gengodb::semantics::xsd::Type::AnyIRI));
         b.create<tuples::ReturnOp>(loc, mlir::Value(b.create<variant::CreateScalarOp>(loc, variant::VariantType::get(ctxt), str, typeId)));
      });
      auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), nestedMapOp.getRes(), rewriter.getArrayAttr({valueDef}), helper.getColRefs());
      mapOp.getFn().push_back(helper.getMapBlock());
      anchorValueRef = valueRef;
      return mapOp.getResult();
   }

   // keeps tuples with left = right (SPARQL term equality, as TripleEmitter::filterVariantsEqual)
   mlir::Value filterVariantsEqual(mlir::Value stream, tuples::ColumnRefAttr left, tuples::ColumnRefAttr right) {
      subop::MapCreationHelper helper(ctxt);
      auto [eqDef, eqRef] = createColumn(rewriter.getI1Type(), "path", "same");
      helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
         auto cmpType = db::NullableType::get(ctxt, b.getI1Type());
         mlir::Value cmp = b.create<variant::CmpOp>(loc, cmpType, variant::VariantCmpPredicate::eq, helper.access(left, loc), helper.access(right, loc));
         b.create<tuples::ReturnOp>(loc, mlir::Value(b.create<db::DeriveTruth>(loc, b.getI1Type(), cmp)));
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

class PathExpansionLowering : public mlir::OpConversionPattern<gpm::PathExpansionOp> {
   NamedGraphMapper& graphs;
   ExternalGraphMapper& externalGraphs;
   PathInputColumns& inputColumns;

   public:
   PathExpansionLowering(mlir::TypeConverter& typeConverter, mlir::MLIRContext* context, NamedGraphMapper& graphs, ExternalGraphMapper& externalGraphs, PathInputColumns& inputColumns)
      : mlir::OpConversionPattern<gpm::PathExpansionOp>(typeConverter, context), graphs(graphs), externalGraphs(externalGraphs), inputColumns(inputColumns) {}

   mlir::LogicalResult matchAndRewrite(gpm::PathExpansionOp pathOp, OpAdaptor adaptor, mlir::ConversionPatternRewriter& rewriter) const override {
      auto* ctxt = rewriter.getContext();
      auto loc = pathOp.getLoc();
      auto& columnManager = ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto tupleStreamType = tuples::TupleStreamType::get(ctxt);
      auto i1 = rewriter.getI1Type();
      auto i32 = rewriter.getI32Type();

      if (auto anchorVar = mlir::dyn_cast<gpm::VariableTermAttr>(pathOp.getAnchorTerm())) {
         if (mlir::isa<db::NullableType>(anchorVar.getBindingReference().getColumn().type))
            return pathOp.emitOpError() << "nullable anchors not yet supported";
      }
      auto targetVar = mlir::cast<gpm::VariableTermAttr>(pathOp.getTarget());
      if (targetVar.hasBinding() && mlir::isa<db::NullableType>(targetVar.getBindingReference().getColumn().type))
         return pathOp.emitOpError() << "nullable bound targets not yet supported";
      auto inputIt = inputColumns.find(pathOp.getOperation());
      if (inputIt == inputColumns.end())
         return pathOp.emitOpError() << "input columns were not collected before lowering";
      const relalg::ColumnSet& rColumns = inputIt->second;
      bool outgoing = pathOp.getAnchor() == gpm::ExpansionDirection::outgoing;
      // A fresh target may carry `fromExisting` (gpm-unnest-patterns: a variable also bound outside the
      // pattern, matched by a bindings-compatible join above this op). The op itself just defines the column.
      tuples::ColumnDefAttr targetDefAttr = targetVar.hasBinding() ? tuples::ColumnDefAttr() : columnManager.createDef(&targetVar.getProducedBinding().getColumn());
      auto predicates = llvm::to_vector(pathOp.getPredicates().getValue());
      auto paramId = pathOp.getParamId();

      auto& ext = getOrCreateExternalGraph(rewriter, loc, pathOp.getGraphRef(), externalGraphs, rewriter.getInsertionBlock());
      NamedGraphMapper localGraphs;
      localGraphs[pathOp.getGraphRef().getName()] = NamedGraph{ext.graphType, nullptr, nullptr, ext.externalGraph};
      PathEmitter emitter(rewriter, localGraphs, pathOp, pathOp.getGraphRef(), ext);

      // ───── Phase 0: materialize R ─────
      llvm::SmallVector<Member> rMembers;
      llvm::SmallVector<subop::RefMappingPairT> rRefMapping;
      llvm::SmallVector<subop::DefMappingPairT> rDefMapping;
      llvm::DenseMap<const tuples::Column*, Member> rMemberOf;
      for (auto* column : rColumns) {
         auto member = createMember(ctxt, "path_r", column->type);
         rMembers.push_back(member);
         rMemberOf.insert({column, member});
         rRefMapping.push_back({member, columnManager.createRef(column)});
         rDefMapping.push_back({member, columnManager.createDef(column)});
      }
      mlir::Value rStream = adaptor.getRel();
      if (rMembers.empty()) {
         // e.g. a constant anchor straight on the named graph: a zero-width buffer doesn't
         // reliably keep its tuple count, so materialize a constant marker column
         auto [markerDef, markerRef] = createColumn(i1, "path", "marker");
         subop::MapCreationHelper helper(ctxt);
         helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
            b.create<tuples::ReturnOp>(loc, mlir::Value(b.create<mlir::arith::ConstantIntOp>(loc, 1, i1)));
         });
         auto mapOp = rewriter.create<subop::MapOp>(loc, tupleStreamType, rStream, rewriter.getArrayAttr({markerDef}), helper.getColRefs());
         mapOp.getFn().push_back(helper.getMapBlock());
         rStream = mapOp.getResult();
         auto member = createMember(ctxt, "path_r_marker", i1);
         rMembers.push_back(member);
         rRefMapping.push_back({member, markerRef});
      }
      auto rType = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, rMembers));
      mlir::Value rBuffer = rewriter.create<subop::GenericCreateOp>(loc, rType);
      rewriter.create<subop::MaterializeOp>(loc, rStream, rBuffer, subop::ColumnRefMemberMappingAttr::get(ctxt, rRefMapping));

      // ───── Phase 1: distinct anchor ids -> frontier0 = {(a, a)} ─────
      mlir::Value anchorStream = emitter.scanGraph();
      mlir::Attribute phase1Anchor = pathOp.getAnchorTerm();
      if (auto anchorVar = mlir::dyn_cast<gpm::VariableTermAttr>(phase1Anchor)) {
         auto* anchorColumn = &anchorVar.getBindingReference().getColumn();
         auto [anchorDef, anchorRef] = createColumn(anchorColumn->type, "path", "anchor");
         auto anchorMember = rMemberOf.at(anchorColumn);
         anchorStream = emitter.attachToGraph(anchorStream, [&]() -> mlir::Value {
            return rewriter.create<subop::ScanOp>(loc, rBuffer, createColumnDefMemberMappingAttr(ctxt, {{anchorMember, anchorDef}}));
         });
         phase1Anchor = gpm::VariableTermAttr::get(ctxt, anchorRef);
      }
      tuples::ColumnRefAttr anchorIdRef;
      anchorStream = emitter.anchorIds(anchorStream, phase1Anchor, paramId, anchorIdRef);

      auto anchorKey = createMember(ctxt, "path_anchor", i32);
      auto anchorsType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {anchorKey}), createStateMembersAttr(ctxt, {}), false);
      mlir::Value anchors = rewriter.create<subop::GenericCreateOp>(loc, anchorsType);
      {
         auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, anchorsType), "path", "anchorref");
         auto insertAnchors = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, anchorStream, anchors, rewriter.getArrayAttr({anchorIdRef}), refDef);
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
      bool zeroLength = pathOp.getMinLength() == 0;
      if (zeroLength) {
         // p*: every anchor in G reaches itself; seen = true so the loop never re-emits (a, a)
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
         stepStream = emitter.step(stepStream, nRef, outgoing, predicates, mRef);
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

      // ───── %reach (start -> end) from %visited, built after the loop ─────
      auto reachStart = createMember(ctxt, "path_m_start", i32);
      auto reachNode = createMember(ctxt, "path_m_node", i32);
      auto reachType = subop::MultiMapType::get(ctxt, createStateMembersAttr(ctxt, {reachStart}), createStateMembersAttr(ctxt, {reachNode}));
      mlir::Value reach = rewriter.create<subop::GenericCreateOp>(loc, reachType);
      {
         auto [vsDef, vsRef] = createColumn(i32, "path", "visited_start");
         auto [vnDef, vnRef] = createColumn(i32, "path", "visited_node");
         mlir::Value scan = rewriter.create<subop::ScanOp>(loc, visited, createColumnDefMemberMappingAttr(ctxt, {{visitedStart, vsDef}, {visitedNode, vnDef}}));
         auto insertOp = rewriter.create<subop::InsertOp>(loc, scan, reach, subop::ColumnRefMemberMappingAttr::get(ctxt, {{reachStart, vsRef}, {reachNode, vnRef}}));
         insertOp.getEqFn().push_back(createI32EqBlock(rewriter, loc, 1));
      }

      // ───── Phase 3: R ⋈ reach ─────
      // Re-define the graph's existing set columns if ops downstream were lowered against them.
      auto graphIt = graphs.find(pathOp.getGraphRef().getName());
      mlir::Value graphStream = emitter.scanGraph(graphIt != graphs.end() ? &graphIt->second : nullptr);
      if (graphIt == graphs.end()) graphs[pathOp.getGraphRef().getName()] = emitter.current();
      NamedGraph phase3Graph = emitter.current();
      mlir::Value out = emitter.attachToGraph(graphStream, [&]() -> mlir::Value {
         return rewriter.create<subop::ScanOp>(loc, rBuffer, createColumnDefMemberMappingAttr(ctxt, rDefMapping));
      });
      tuples::ColumnRefAttr startIdRef;
      out = emitter.anchorIds(out, pathOp.getAnchorTerm(), paramId, startIdRef);
      auto entryRefType = subop::MultiMapEntryRefType::get(ctxt, reachType);
      auto listType = subop::ListType::get(ctxt, entryRefType);
      auto [listDef, listRef] = createColumn(listType, "path", "list");
      auto lookupReach = rewriter.create<subop::LookupOp>(loc, tupleStreamType, out, reach, rewriter.getArrayAttr({startIdRef}), listDef);
      lookupReach.getEqFn().push_back(createI32EqBlock(rewriter, loc, 1));
      auto [endIdDef, endIdRef] = createColumn(i32, "path", "end");
      {
         auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tupleStreamType, lookupReach.getRes(), rewriter.getArrayAttr({listRef}));
         auto* block = new mlir::Block;
         block->addArgument(tuples::TupleType::get(ctxt), loc);
         mlir::Value list = block->addArgument(listType, loc);
         nestedMapOp.getRegion().push_back(block);
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         auto [entryDef, entryRef] = createColumn(entryRefType, "path", "entry");
         mlir::Value entries = rewriter.create<subop::ScanListOp>(loc, list, entryDef);
         mlir::Value gathered = rewriter.create<subop::GatherOp>(loc, entries, entryRef, createColumnDefMemberMappingAttr(ctxt, {{reachNode, endIdDef}}));
         rewriter.create<tuples::ReturnOp>(loc, gathered);
         out = nestedMapOp.getRes();
      }
      if (targetVar.hasBinding()) {
         // both ends bound: keep the tuple iff its target node is reachable
         tuples::ColumnRefAttr targetIdRef;
         out = emitter.anchorIds(out, targetVar, std::nullopt, targetIdRef);
         auto [eqDef, eqRef] = createColumn(i1, "path", "reached");
         subop::MapCreationHelper helper(ctxt);
         helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
            mlir::Value eq = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, helper.access(endIdRef, loc), helper.access(targetIdRef, loc));
            b.create<tuples::ReturnOp>(loc, eq);
         });
         auto mapOp = rewriter.create<subop::MapOp>(loc, tupleStreamType, out, rewriter.getArrayAttr({eqDef}), helper.getColRefs());
         mapOp.getFn().push_back(helper.getMapBlock());
         out = rewriter.create<subop::FilterOp>(loc, mapOp.getResult(), subop::FilterSemantic::all_true, rewriter.getArrayAttr({eqRef}));
      } else {
         auto targetDef = targetDefAttr;
         targetDef.getColumn().type = variant::VariantType::get(ctxt);
         gsubop::NodeRefType nodeRefType;
         tuples::ColumnRefAttr nodeRef;
         out = emitter.lookupNode(out, endIdRef, nodeRefType, nodeRef);
         out = emitter.wrapNode(out, nodeRef, targetDef);
      }

      // ───── p*: anchors that aren't nodes of G still reach themselves (zero-length path) ─────
      if (zeroLength) {
         mlir::Value outside = emitter.attachToGraph(emitter.scanGraph(&phase3Graph), [&]() -> mlir::Value {
            return rewriter.create<subop::ScanOp>(loc, rBuffer, createColumnDefMemberMappingAttr(ctxt, rDefMapping));
         });
         tuples::ColumnRefAttr anchorValueRef;
         outside = emitter.anchorsNotInGraph(outside, pathOp.getAnchorTerm(), paramId, anchorValueRef);
         if (targetVar.hasBinding()) {
            outside = emitter.filterVariantsEqual(outside, anchorValueRef, targetVar.getBindingReference());
         } else {
            subop::MapCreationHelper helper(ctxt);
            helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) { b.create<tuples::ReturnOp>(loc, helper.access(anchorValueRef, loc)); });
            auto mapOp = rewriter.create<subop::MapOp>(loc, tupleStreamType, outside, rewriter.getArrayAttr({targetDefAttr}), helper.getColRefs());
            mapOp.getFn().push_back(helper.getMapBlock());
            outside = mapOp.getResult();
         }
         out = rewriter.create<subop::UnionOp>(loc, tupleStreamType, mlir::ValueRange{out, outside});
      }

      // relalg consumers need the stream re-typed as a relalg operator (cf. replaceTripleOp)
      bool needsInFlight = llvm::any_of(pathOp.getRes().getUses(), [](mlir::OpOperand& use) { return isRelAlgOperator(use.getOwner()); });
      if (needsInFlight) {
         relalg::ColumnSet available = rColumns;
         available.insert(pathOp.getCreatedColumns());
         auto inFlight = rewriter.create<relalg::InFlightOp>(loc, out, available.asRefArrayAttr(ctxt));
         rewriter.replaceUsesWithIf(pathOp.getRes(), inFlight.getRes(), [](mlir::OpOperand& use) { return isRelAlgOperator(use.getOwner()); });
      }
      rewriter.replaceOp(pathOp, out);
      return mlir::success();
   }
};
} // namespace

PathInputColumns collectPathInputColumns(mlir::ModuleOp module) {
   PathInputColumns result;
   module.walk([&](gpm::PathExpansionOp pathOp) {
      // The target's column type is known up front; set it before the first conversion round so that
      // refreshNullableTypes derives union/outer-join mapping types from it (the op is lowered later).
      auto targetVar = mlir::cast<gpm::VariableTermAttr>(pathOp.getTarget());
      if (!targetVar.hasBinding()) targetVar.getProducedBinding().getColumn().type = variant::VariantType::get(module.getContext());
      relalg::AvailabilityCache cache;
      relalg::ColumnSet columns = pathOp.getAvailableColumns(cache);
      columns.remove(pathOp.getCreatedColumns());
      relalg::ColumnSet materializable;
      for (auto* column : columns) {
         if (!mlir::isa<gpm::GraphReferenceType>(column->type)) materializable.insert(column);
      }
      result[pathOp.getOperation()] = materializable;
   });
   return result;
}

void populatePathExpansionLoweringPatterns(mlir::RewritePatternSet& patterns, mlir::TypeConverter& typeConverter, NamedGraphMapper& graphs, ExternalGraphMapper& externalGraphs, PathInputColumns& inputColumns) {
   patterns.insert<PathExpansionLowering>(typeConverter, patterns.getContext(), graphs, externalGraphs, inputColumns);
}
} // namespace gengodb::compiler::dialect::gpm::lowering
