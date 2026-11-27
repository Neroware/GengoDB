#include "gengodb/compiler/Conversion/GPMToSubOp/GraphPatternHelper.h"

#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/SubOperator/Utils.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;
using namespace helpers;

namespace {
mlir::Value scanNamedGraph(mlir::OpBuilder& builder, mlir::Location loc, NamedGraph& namedGraph, const std::string& group, const std::string& graph, bool uniqueScope = true) {
   auto& columnManager = builder.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
   auto nodeSetDef = createDef(columnManager, group, graph + "_vx", namedGraph.nodeSetColumn->type, uniqueScope);
   auto edgeSetDef = createDef(columnManager, group, graph + "_ex", namedGraph.edgeSetColumn->type, uniqueScope);
   namedGraph.nodeSetColumn = &nodeSetDef.getColumn();
   namedGraph.edgeSetColumn = &edgeSetDef.getColumn();
   return builder.create<gsubop::ScanGraphOp>(loc, namedGraph.externalGraph, nodeSetDef, edgeSetDef);
}
mlir::Block* createI32EqBlock(mlir::OpBuilder& builder, mlir::Location loc, unsigned numKeys) {
    auto* block = new mlir::Block;
    auto i32Type = builder.getI32Type();
    for (unsigned i = 0; i < 2 * numKeys; ++i) block->addArgument(i32Type, loc);
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(block);
    mlir::Value result;
    for (unsigned i = 0; i < numKeys; ++i) {
        mlir::Value eq = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::eq, block->getArgument(i), block->getArgument(numKeys + i));
        result = result ? builder.create<mlir::arith::AndIOp>(loc, result, eq).getResult() : eq;
    }
    builder.create<tuples::ReturnOp>(loc, result);
    return block;
}
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
} // end anonymous namespace

template <typename BuildIdentFn>
mlir::Value generateTupleStream(mlir::ConversionPatternRewriter& rewriter, mlir::Location loc, tuples::ColumnDefAttr def, BuildIdentFn buildIdent) {
    auto ctxt = rewriter.getContext();
    auto generateOp = rewriter.create<subop::GenerateOp>(loc, std::vector<mlir::Type>{
        tuples::TupleStreamType::get(ctxt), tuples::TupleStreamType::get(ctxt)}, rewriter.getArrayAttr({def}));
    auto* generateBlock = new mlir::Block;
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    generateOp.getRegion().push_back(generateBlock);
    rewriter.setInsertionPointToStart(generateBlock);
    mlir::Value identValue = buildIdent(rewriter);
    rewriter.create<subop::GenerateEmitOp>(loc, mlir::ValueRange{identValue});
    rewriter.create<tuples::ReturnOp>(loc);
    return generateOp.getRes();
}
TripleEmitter::TripleEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, mlir::Operation* op, mlir::DictionaryAttr triple)
    : GraphPatternHelper(rewriter, graphs, op, mlir::cast<tuples::ColumnRefAttr>(triple.get("graphRef"))),
    sTerm(triple.get("s")), pTerm(triple.get("p")), oTerm(triple.get("o")),
    truthDegrAttr(mlir::dyn_cast_or_null<tuples::ColumnDefAttr>(triple.get("truthDegr"))) {
    for (auto [i, attr] : llvm::enumerate(mlir::cast<mlir::ArrayAttr>(triple.get("paramIds")))) {
        int64_t id = mlir::cast<mlir::IntegerAttr>(attr).getInt();
        if (id >= 0) paramIds[i] = static_cast<size_t>(id);
    }
}
mlir::Value GraphPatternHelper::emitNodeToEdge(mlir::Value stream, AnchorT anchor, EdgeDirection direction, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef, std::optional<size_t> paramId) {
    gsubop::NodeRefType nodeRefType;
    tuples::ColumnRefAttr nodeRef;
    if (auto* anchorAttr = std::get_if<tuples::ColumnRefAttr>(&anchor)) {
        stream = resolveNode(stream, *anchorAttr, nodeRefType, nodeRef);
    }
    else {
        auto identAttr = std::get<IdentifierTermAttr>(anchor);
        stream = lookupNode(stream, {}, gsubop::IdentifierType::get(ctxt), nodeRefType, nodeRef,
            [&](mlir::OpBuilder&, mlir::Value, mlir::ValueRange) -> mlir::Value {
                return graphHelper.createIdentifier(graph, identAttr, paramId);
            },
            [&](mlir::Value scan, tuples::ColumnRefAttr identRef) { return filterByIdentifier(scan, identRef); });
    }

    auto edgeMember = direction == EdgeDirection::Outgoing ? nodeRefType.getOutgoingMembers().getMembers()[0] : nodeRefType.getIncomingMembers().getMembers()[0];
    auto edgeSetType = memberManager.getType(edgeMember);
    auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", direction == EdgeDirection::Outgoing ? "outgoing" : "incoming");
    stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{edgeMember, edgesDef}}));
    return scanEdges(stream, edgesRef, edgeRefType, edgeRef);
}
mlir::Value GraphPatternHelper::emitEdgeToNode(mlir::Value stream, tuples::ColumnRefAttr edgeRef, Member nodeMember, tuples::ColumnRefAttr& rawRef) {
    auto [rawDef, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "raw");
    rawRef = ref;
    return rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, rawDef}}));
}
mlir::Value GraphPatternHelper::scanEdges(mlir::Value stream, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef) {
    auto& graphData = graphs[graphName];
    return scanEdges(stream, columnManager.createRef(graphData.edgeSetColumn), edgeRefType, edgeRef);
}
mlir::Value GraphPatternHelper::scanEdges(mlir::Value stream, tuples::ColumnRefAttr edgesRef, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef) {
    edgeRefType = createEdgeRefType(ctxt, group, graph, useEdgeWeights ? mlir::Float32Type::get(ctxt) : mlir::Type());
    auto [edgeRefColumnDef, resolvedRef] = createColumn(edgeRefType, "edges", "ref");
    edgeRefColumnRef = resolvedRef;
    return graphHelper.attach(stream, {edgesRef}, [&](mlir::Value, mlir::ValueRange args) -> mlir::Value {
        return rewriter.create<gsubop::ScanEdgeSetOp>(loc, args[0], edgeRefColumnDef);
    });
}
mlir::Value GraphPatternHelper::filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr ref, IdentifierTermAttr ident, std::optional<size_t> paramId) {
    auto identOp = graphHelper.createIdentifier(graph, ident, paramId);
    return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, mlir::ValueRange{identOp});
}
mlir::Value GraphPatternHelper::filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr identRef) {
    subop::MapCreationHelper helper(ctxt);
    auto [validDef, validRef] = createColumn(rewriter.getI1Type(), "idents", "valid");
    helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
        mlir::Value identVal = helper.access(identRef, loc);
        mlir::Value valid;
        if (mlir::isa<gsubop::IdentifierType>(identVal.getType())) {
            valid = b.create<gsubop::IdentifierValidOp>(loc, b.getI1Type(), identVal);
        }
        else {
            mlir::Value zero = b.create<mlir::arith::ConstantIntOp>(loc, 0, identVal.getType());
            valid = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sge, identVal, zero);
        }
        b.create<tuples::ReturnOp>(loc, valid);
    });
    auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({validDef}), helper.getColRefs());
    mapOp.getFn().push_back(helper.getMapBlock());
    return rewriter.create<subop::FilterOp>(loc, mapOp.getResult(), subop::FilterSemantic::all_true, rewriter.getArrayAttr({validRef}));
}
mlir::Value GraphPatternHelper::wrapNode(mlir::Value stream, tuples::ColumnRefAttr rawRef, tuples::ColumnDefAttr variantDef) {
    subop::MapCreationHelper helper(ctxt);
    helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
        mlir::Value rawVal = helper.access(rawRef, loc);
        mlir::Value variantVal = b.create<variant::CreateNodeRefOp>(loc, variant::VariantType::get(ctxt), rawVal);
        b.create<tuples::ReturnOp>(loc, variantVal);
    });
    auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({variantDef}), helper.getColRefs());
    mapOp.getFn().push_back(helper.getMapBlock());
    return mapOp.getResult();
}
mlir::Value GraphPatternHelper::wrapEdge(mlir::Value stream, tuples::ColumnRefAttr edgeRef, tuples::ColumnDefAttr variantDef) {
    subop::MapCreationHelper helper(ctxt);
    helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
        mlir::Value edgeVal = helper.access(edgeRef, loc);
        mlir::Value variantVal = b.create<variant::CreateEdgeRefOp>(loc, variant::VariantType::get(ctxt), edgeVal);
        b.create<tuples::ReturnOp>(loc, variantVal);
    });
    auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({variantDef}), helper.getColRefs());
    mapOp.getFn().push_back(helper.getMapBlock());
    return mapOp.getResult();
}
mlir::Value GraphPatternHelper::lookupNode(mlir::Value stream, llvm::ArrayRef<tuples::ColumnRefAttr> columns, mlir::Type keyType,
    gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef, BuildKeyFn buildKey, RefApplyFn refineScan, RefApplyFn refineLookup) {
        nodeRefType = createNodeRefType(ctxt, group, graph);
        llvm::SmallVector<tuples::ColumnRefAttr> refs{columnManager.createRef(graphs[graphName].nodeSetColumn)};
        refs.append(columns.begin(), columns.end());
        auto [nodeDef, ref] = createColumn(nodeRefType, "nodes", "ref");
        nodeRef = ref;
        return graphHelper.attach(stream, refs, [&](mlir::Value, mlir::ValueRange args) -> mlir::Value {
            mlir::Value nodeSetArg = args[0];
            auto [keyDef, keyRef] = createColumn(keyType, "nodes", "lookup");
            mlir::Value scan = generateTupleStream(rewriter, loc, keyDef, [&](mlir::OpBuilder& b) -> mlir::Value {
                return buildKey(b, nodeSetArg, args.drop_front());
            });
            if (refineScan) scan = refineScan(scan, keyRef);
            mlir::Value result = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, nodeSetArg, rewriter.getArrayAttr({keyRef}), nodeDef);
            return refineLookup ? refineLookup(result, nodeRef) : result;
    });
}
mlir::Value GraphPatternHelper::lookupEdge(mlir::Value stream, llvm::ArrayRef<tuples::ColumnRefAttr> columns, mlir::Type keyType,
    gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef, BuildKeyFn buildKey, RefApplyFn refineScan, RefApplyFn refineLookup) {
        edgeRefType = createEdgeRefType(ctxt, group, graph, useEdgeWeights ? mlir::Float32Type::get(ctxt) : mlir::Type());
        llvm::SmallVector<tuples::ColumnRefAttr> refs{columnManager.createRef(graphs[graphName].edgeSetColumn)};
        refs.append(columns.begin(), columns.end());
        auto [edgeDef, ref] = createColumn(edgeRefType, "edges", "ref");
        edgeRef = ref;
        return graphHelper.attach(stream, refs, [&](mlir::Value, mlir::ValueRange args) -> mlir::Value {
            mlir::Value edgeSetArg = args[0];
            auto [keyDef, keyRef] = createColumn(keyType, "edges", "lookup");
            mlir::Value scan = generateTupleStream(rewriter, loc, keyDef, [&](mlir::OpBuilder& b) -> mlir::Value {
                return buildKey(b, edgeSetArg, args.drop_front());
            });
            if (refineScan) scan = refineScan(scan, keyRef);
            mlir::Value result = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, edgeSetArg, rewriter.getArrayAttr({keyRef}), edgeDef);
            return refineLookup ? refineLookup(result, edgeRef) : result;
        });
}
mlir::Value GraphPatternHelper::resolveEdge(mlir::Value stream, tuples::ColumnRefAttr edgeVariantRef, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef) {
    return lookupEdge(stream, {edgeVariantRef}, rewriter.getI32Type(), edgeRefType, edgeRef,
        [&](mlir::OpBuilder& b, mlir::Value edgeSetArg, mlir::ValueRange args) -> mlir::Value {
            return b.create<variant::ResolveEdgeOp>(loc, b.getI32Type(), edgeSetArg, args[0]);
        },
        [&](mlir::Value scan, tuples::ColumnRefAttr idRef) { 
            return filterByIdentifier(scan, idRef); 
        });
}
mlir::Value GraphPatternHelper::resolveNode(mlir::Value stream, tuples::ColumnRefAttr nodeVariantRef, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef) {
    return lookupNode(stream, {nodeVariantRef}, rewriter.getI32Type(), nodeRefType, nodeRef,
        [&](mlir::OpBuilder& b, mlir::Value nodeSetArg, mlir::ValueRange args) -> mlir::Value {
            return b.create<variant::ResolveNodeOp>(loc, b.getI32Type(), nodeSetArg, args[0]);
        },
        [&](mlir::Value scan, tuples::ColumnRefAttr idRef) { 
            return filterByIdentifier(scan, idRef); 
        });
}
mlir::Value GraphPatternHelper::nodeId(mlir::Value stream, IdentifierTermAttr ident, tuples::ColumnRefAttr& idRef, std::optional<size_t> paramId) {
    gsubop::NodeRefType nodeRefType;
    tuples::ColumnRefAttr nodeRef;
    stream = lookupNode(stream, {}, gsubop::IdentifierType::get(ctxt), nodeRefType, nodeRef,
        [&](mlir::OpBuilder&, mlir::Value, mlir::ValueRange) -> mlir::Value {
            return graphHelper.createIdentifier(graph, ident, paramId);
        },
        [&](mlir::Value scan, tuples::ColumnRefAttr identRef) {
            return filterByIdentifier(scan, identRef);
        });
    return graphHelper.nodeId(stream, nodeRef, nodeRefType, idRef);
}
mlir::Value GraphPatternHelper::lookupNode(mlir::Value stream, tuples::ColumnRefAttr idRef, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef) {
    return lookupNode(stream, {idRef}, rewriter.getI32Type(), nodeRefType, nodeRef,
        [](mlir::OpBuilder&, mlir::Value, mlir::ValueRange args) -> mlir::Value { return args[0]; });
}
mlir::Value GraphPatternHelper::filterByIdentifiers(mlir::Value stream, tuples::ColumnRefAttr edgeRef, llvm::ArrayRef<mlir::Attribute> predicates) {
    llvm::SmallVector<mlir::Value> idents;
    for (auto predicate : predicates) {
        idents.push_back(graphHelper.createIdentifier(graph, mlir::cast<IdentifierTermAttr>(predicate), std::nullopt));
    }
    return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, idents);
}
mlir::Value GraphPatternHelper::step(mlir::Value stream, tuples::ColumnRefAttr fromRef, bool outgoing, llvm::ArrayRef<mlir::Attribute> predicates, tuples::ColumnRefAttr& toRef) {
    // gsubop::NodeRefType nodeRefType;
    // tuples::ColumnRefAttr nodeRef;
    // stream = lookupNode(stream, fromRef, nodeRefType, nodeRef);
    // auto edgeMember = outgoing ? nodeRefType.getOutgoingMembers().getMembers()[0] : nodeRefType.getIncomingMembers().getMembers()[0];
    // auto edgeSetType = memberManager.getType(edgeMember);
    // auto [edgesDef, edgesRef] = createColumn(edgeSetType, "edges", outgoing ? "outgoing" : "incoming");
    // stream = rewriter.create<subop::GatherOp>(loc, stream, nodeRef, createColumnDefMemberMappingAttr(ctxt, {{edgeMember, edgesDef}}));
    // gsubop::EdgeRefType edgeRefType;
    // tuples::ColumnRefAttr edgeRef;
    // stream = scanEdges(stream, edgesRef, edgeSetType, edgeRefType, edgeRef);
    // stream = filterByIdentifiers(stream, edgeRef, predicates);
    // auto endpointMember = outgoing ? edgeRefType.getToMembers().getMembers()[0] : edgeRefType.getFromMembers().getMembers()[0];
    // tuples::ColumnRefAttr rawRef;
    // stream = emitEdgeToNode(stream, edgeRef, endpointMember, rawRef);
    // return nodeIds(stream, rawRef, mlir::cast<gsubop::NodeRefType>(memberManager.getType(endpointMember)), toRef);
}

mlir::Value TripleEmitter::lower(mlir::Value stream, std::optional<gpm::TripleSlot> anchor) {
    gsubop::EdgeRefType edgeRefType;
    tuples::ColumnRefAttr edgeRef;
    bool anchorSubject = anchor.has_value() ? *anchor == gpm::TripleSlot::subject : mlir::isa<gpm::IdentifierTermAttr>(sTerm);
    bool anchorObject = anchor.has_value() ? *anchor == gpm::TripleSlot::object : !anchorSubject && mlir::isa<gpm::IdentifierTermAttr>(oTerm);
    if (anchorSubject) {
        stream = emitNodeToEdge(stream, anchorAttr(sTerm), EdgeDirection::Outgoing, edgeRefType, edgeRef, paramIds[0]);
        stream = emitPredicate(stream, edgeRef, paramIds[1]);
        stream = emitTerm(stream, oTerm, edgeRefType.getToMembers().getMembers()[0], edgeRef, paramIds[2]);
    }
    else if (anchorObject) {
        stream = emitNodeToEdge(stream, anchorAttr(oTerm), EdgeDirection::Incoming, edgeRefType, edgeRef, paramIds[2]);
        stream = emitTerm(stream, sTerm, edgeRefType.getFromMembers().getMembers()[0], edgeRef, paramIds[0]);
        stream = emitPredicate(stream, edgeRef, paramIds[1]);
    }
    else {
        stream = scanEdges(stream, edgeRefType, edgeRef);
        stream = emitTerm(stream, sTerm, edgeRefType.getFromMembers().getMembers()[0], edgeRef, paramIds[0]);
        stream = emitPredicate(stream, edgeRef, paramIds[1]);
        stream = emitTerm(stream, oTerm, edgeRefType.getToMembers().getMembers()[0], edgeRef, paramIds[2]);
    }
    if (truthDegrAttr) {
        stream = emitTruthDegree(stream, edgeRefType, edgeRef);
    }
    return stream;
}
mlir::Value TripleEmitter::emitTruthDegree(mlir::Value stream, gsubop::EdgeRefType edgeRefType, tuples::ColumnRefAttr edgeRef) {
    auto truthMember = edgeRefType.getPropertyMembers().getMembers()[0];
    assert(mlir::isa<mlir::Float32Type>(memberManager.getType(truthMember)) && "edge ref must expose its payload as truth degree");
    assert(mlir::isa<mlir::Float32Type>(truthDegrAttr.getColumn().type) && "truth degree columns need type f32");
    return rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{truthMember, truthDegrAttr}}));
}
GraphPatternHelper::AnchorT TripleEmitter::anchorAttr(mlir::Attribute term) {
    if (auto identTerm = mlir::dyn_cast<gpm::IdentifierTermAttr>(term))
        return identTerm;
    auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(term);
    if (varTerm && varTerm.getProducedBinding()) {
        if (auto it = localTerms.find(&varTerm.getProducedBinding().getColumn()); it != localTerms.end())
            return it->second;
    }
    if (!varTerm || !varTerm.hasBinding())
        assert(false && "anchor lacks binding");
    return varTerm.getBindingReference();
}
mlir::Value TripleEmitter::filterVariantsEqual(mlir::Value stream, tuples::ColumnRefAttr left, tuples::ColumnRefAttr right) {
    subop::MapCreationHelper helper(ctxt);
    auto [eqDef, eqRef] = createColumn(rewriter.getI1Type(), "map", "selfeq");
    helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
        mlir::Value lhs = helper.access(left, loc);
        mlir::Value rhs = helper.access(right, loc);
        auto cmpType = db::NullableType::get(ctxt, b.getI1Type());
        mlir::Value cmp = b.create<variant::CmpOp>(loc, cmpType, variant::VariantCmpPredicate::eq, lhs, rhs);
        mlir::Value truth = b.create<db::DeriveTruth>(loc, b.getI1Type(), cmp);
        b.create<tuples::ReturnOp>(loc, truth);
    });
    auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({eqDef}), helper.getColRefs());
    mapOp.getFn().push_back(helper.getMapBlock());
    return rewriter.create<subop::FilterOp>(loc, mapOp.getResult(), subop::FilterSemantic::all_true, rewriter.getArrayAttr({eqRef}));
}
mlir::Value TripleEmitter::finish(mlir::Value stream, mlir::Attribute term, tuples::ColumnRefAttr rawRef) {
    const auto* column = &mlir::cast<VariableTermAttr>(term).getProducedBinding().getColumn();
    if (auto it = localTerms.find(column); it != localTerms.end()) {
        auto [tmpDef, tmpRef] = createColumn(variant::VariantType::get(ctxt), "nodes", "tmp");
        stream = wrapNode(stream, rawRef, tmpDef);
        return filterVariantsEqual(stream, tmpRef, it->second);
    }
    auto variantDef = columnManager.createDef(column);
    assert(mlir::isa<variant::VariantType>(variantDef.getColumn().type) && "column type needs to be variant");
    stream = wrapNode(stream, rawRef, variantDef);
    localTerms[column] = columnManager.createRef(column);
    return stream;
}
mlir::Value TripleEmitter::emitPredicate(mlir::Value stream, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId) {
    if (auto constPred = mlir::dyn_cast<IdentifierTermAttr>(pTerm)) {
        return filterByIdentifier(stream, edgeRef, constPred, paramId);
    }
    gsubop::NodeRefType nodeRefType;
    tuples::ColumnRefAttr rawRef;
    stream = lookupNode(stream, {edgeRef}, gsubop::IdentifierType::get(ctxt), nodeRefType, rawRef,
        [&](mlir::OpBuilder& b, mlir::Value, mlir::ValueRange args) -> mlir::Value {
            return b.create<gsubop::GetIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), args[0]);
        });
    return finish(stream, pTerm, rawRef);
}
mlir::Value TripleEmitter::emitTerm(mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId) {
    tuples::ColumnRefAttr rawRef;
    stream = emitEdgeToNode(stream, edgeRef, nodeMember, rawRef);
    if (auto constTerm = mlir::dyn_cast<IdentifierTermAttr>(term)) {
        return filterByIdentifier(stream, rawRef, constTerm, paramId);
    }
    return finish(stream, term, rawRef);
}

mlir::Value PathPatternEmitter::emitFrontier(Member& anchorKey, mlir::Value& anchors) {
    auto i32Type = rewriter.getI32Type();
    auto tupleStreamType = tuples::TupleStreamType::get(ctxt);
    auto anchorsType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {anchorKey}), createStateMembersAttr(ctxt, {}), false);
    anchors = rewriter.create<subop::GenericCreateOp>(loc, anchorsType);
    for (auto [anchorStream, anchorIdRef] : this->anchors) {
        auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, anchorsType), "path", "anchorref");
        auto insertAnchors = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, anchorStream, anchors, rewriter.getArrayAttr({anchorIdRef}), refDef);
        insertAnchors.getEqFn().push_back(createI32EqBlock(rewriter, loc, 1));
        insertAnchors.getInitFn().push_back(createInitBlock(rewriter, loc, {}));
    }
    auto f0Start = createMember(ctxt, "path_f0_start", i32Type);
    auto f0Node = createMember(ctxt, "path_f0_node", i32Type);
    auto frontierType = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, {f0Start, f0Node}));
    mlir::Value frontier = rewriter.create<subop::GenericCreateOp>(loc, frontierType);
    {
        auto [idDef, idRef] = createColumn(i32Type, "path", "anchor_id");
        mlir::Value scan = rewriter.create<subop::ScanOp>(loc, anchors, createColumnDefMemberMappingAttr(ctxt, {{anchorKey, idDef}}));
        rewriter.create<subop::MaterializeOp>(loc, scan, frontier, subop::ColumnRefMemberMappingAttr::get(ctxt, {{f0Start, idRef}, {f0Node, idRef}}));
    }
    return frontier;
}
mlir::Value PathPatternEmitter::emitVisited(const Member& anchorKey, mlir::Value anchors, bool zeroLength) {
    auto i32Type = rewriter.getI32Type();
    auto i1Type = rewriter.getI1Type();
    auto tupleStreamType = tuples::TupleStreamType::get(ctxt);
    auto visitedStart = createMember(ctxt, "path_v_start", i32Type);
    auto visitedNode = createMember(ctxt, "path_v_node", i32Type);
    auto visitedSeen = createMember(ctxt, "path_v_seen", i1Type);
    auto visitedType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {visitedStart, visitedNode}), createStateMembersAttr(ctxt, {visitedSeen}), false);
    mlir::Value visited = rewriter.create<subop::GenericCreateOp>(loc, visitedType);
    if (zeroLength) {
        auto [idDef, idRef] = createColumn(i32Type, "path", "anchor_self");
        mlir::Value scan = rewriter.create<subop::ScanOp>(loc, anchors, createColumnDefMemberMappingAttr(ctxt, {{anchorKey, idDef}}));
        auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, visitedType), "path", "selfref");
        auto seed = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, scan, visited, rewriter.getArrayAttr({idRef, idRef}), refDef);
        seed.getEqFn().push_back(createI32EqBlock(rewriter, loc, 2));
        seed.getInitFn().push_back(createInitBlock(rewriter, loc, {{i1Type, 1}}));
    }
}
mlir::Value PathPatternEmitter::emitStep(mlir::Value frontier) {
    // auto i32Type = rewriter.getI32Type();
    // auto i1Type = rewriter.getI1Type();
    // auto tupleStreamType = tuples::TupleStreamType::get(ctxt);
    // auto fStart = createMember(ctxt, "path_f_start", i32Type);
    // auto fNode = createMember(ctxt, "path_f_node", i32Type);
    // auto frontierType = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, {fStart, fNode}));
    // auto loopOp = rewriter.create<subop::LoopOp>(loc, mlir::TypeRange{frontierType}, mlir::ValueRange{frontier});
    // auto* body = new mlir::Block;
    // mlir::Value frontierArg = body->addArgument(frontierType, loc);
    // loopOp.getBodyRegion().push_back(body);
    // {
    //     mlir::OpBuilder::InsertionGuard guard(rewriter);
    //     rewriter.setInsertionPointToStart(body);
    //     auto nStart = createMember(ctxt, "path_n_start", i32Type);
    //     auto nNode = createMember(ctxt, "path_n_node", i32Type);
    //     auto nextType = subop::BufferType::get(ctxt, createStateMembersAttr(ctxt, {nStart, nNode}));
    //     mlir::Value next = rewriter.create<subop::GenericCreateOp>(loc, nextType);
    //     auto cStart = createMember(ctxt, "path_c_start", i32Type);
    //     auto cNode = createMember(ctxt, "path_c_node", i32Type);
    //     auto candType = subop::MapType::get(ctxt, createStateMembersAttr(ctxt, {cStart, cNode}), createStateMembersAttr(ctxt, {}), false);
    //     mlir::Value cand = rewriter.create<subop::GenericCreateOp>(loc, candType);
    //     auto goMember = createMember(ctxt, "path_go", i1Type);
    //     auto goType = subop::SimpleStateType::get(ctxt, createStateMembersAttr(ctxt, {goMember}));
    //     auto goCreate = rewriter.create<subop::CreateSimpleStateOp>(loc, goType);
    //     goCreate.getInitFn().push_back(createInitBlock(rewriter, loc, {{i1Type, 0}}));
    //     mlir::Value go = goCreate.getRes();

    //     // step (s, n) -p-> (s, m), deduplicated within the iteration
    //     auto [sDef, sRef] = createColumn(i32Type, "path", "start");
    //     auto [nDef, nRef] = createColumn(i32Type, "path", "node");
    //     NamedGraph& namedGraph = getGraph();
    //     mlir::Value stepStream = graphHelper.attachGraph(scanNamedGraph(rewriter, loc, namedGraph, group, graph), namedGraph, [&](mlir::Value, mlir::Value, mlir::Value) -> mlir::Value {
    //         return rewriter.create<subop::ScanOp>(loc, frontierArg, createColumnDefMemberMappingAttr(ctxt, {{fStart, sDef}, {fNode, nDef}}));
    //     });
    //     tuples::ColumnRefAttr mRef;
    //     stepStream = emitter.step(stepStream, nRef, forward, predicates, mRef);
    //     {
    //         auto [refDef, refRef] = createColumn(subop::LookupEntryRefType::get(ctxt, candType), "path", "candref");
    //         auto insertCand = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, stepStream, cand, rewriter.getArrayAttr({sRef, mRef}), refDef);
    //         insertCand.getEqFn().push_back(createI32EqBlock(rewriter, loc, 2));
    //         insertCand.getInitFn().push_back(createInitBlock(rewriter, loc, {}));
    //     }

    //     // 2b. Δ = cand − visited ; visited ∪= Δ ; next = Δ
    //     auto [dsDef, dsRef] = createColumn(i32, "path", "delta_start");
    //     auto [dnDef, dnRef] = createColumn(i32, "path", "delta_node");
    //     mlir::Value delta = rewriter.create<subop::ScanOp>(loc, cand, createColumnDefMemberMappingAttr(ctxt, {{cStart, dsDef}, {cNode, dnDef}}));
    //     auto [visRefDef, visRef] = createColumn(subop::LookupEntryRefType::get(ctxt, visitedType), "path", "visref");
    //     auto lookupVisited = rewriter.create<subop::LookupOrInsertOp>(loc, tupleStreamType, delta, visited, rewriter.getArrayAttr({dsRef, dnRef}), visRefDef);
    //     lookupVisited.getEqFn().push_back(createI32EqBlock(rewriter, loc, 2));
    //     lookupVisited.getInitFn().push_back(createInitBlock(rewriter, loc, {{i1, 0}}));
    //     auto [seenDef, seenRef] = createColumn(i1, "path", "seen");
    //     delta = rewriter.create<subop::GatherOp>(loc, lookupVisited.getRes(), visRef, createColumnDefMemberMappingAttr(ctxt, {{visitedSeen, seenDef}}));
    //     delta = rewriter.create<subop::FilterOp>(loc, delta, subop::FilterSemantic::none_true, rewriter.getArrayAttr({seenRef}));
    //     auto [trueDef, trueRef] = createColumn(i1, "path", "true");
    //     {
    //         subop::MapCreationHelper helper(ctxt);
    //         helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
    //         mlir::Value trueValue = b.create<mlir::arith::ConstantIntOp>(loc, 1, i1);
    //         b.create<tuples::ReturnOp>(loc, trueValue);
    //         });
    //         auto mapOp = rewriter.create<subop::MapOp>(loc, tupleStreamType, delta, rewriter.getArrayAttr({trueDef}), helper.getColRefs());
    //         mapOp.getFn().push_back(helper.getMapBlock());
    //         delta = mapOp.getResult();
    //     }
    //     rewriter.create<subop::ScatterOp>(loc, delta, visRef, subop::ColumnRefMemberMappingAttr::get(ctxt, {{visitedSeen, trueRef}}));
    //     rewriter.create<subop::MaterializeOp>(loc, delta, next, subop::ColumnRefMemberMappingAttr::get(ctxt, {{nStart, dsRef}, {nNode, dnRef}}));

    //     // 2c. continue while Δ ≠ ∅
    //     auto [goRefDef, goRef] = createColumn(subop::EntryRefType::get(ctxt, goType), "path", "goref");
    //     mlir::Value goStream = rewriter.create<subop::LookupOp>(loc, tupleStreamType, delta, go, rewriter.getArrayAttr({}), goRefDef);
    //     rewriter.create<subop::ScatterOp>(loc, goStream, goRef, subop::ColumnRefMemberMappingAttr::get(ctxt, {{goMember, trueRef}}));
    //     rewriter.create<subop::LoopContinueOp>(loc, go, subop::MemberAttr::get(ctxt, goMember), mlir::ValueRange{next});
    // }
}

} // namespace gengodb::compiler::dialect::gpm::lowering