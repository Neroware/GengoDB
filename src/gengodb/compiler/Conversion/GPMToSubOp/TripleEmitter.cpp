#include "gengodb/compiler/Conversion/GPMToSubOp/TripleEmitter.h"

#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/SubOperator/Utils.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;
using namespace helpers;

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

mlir::Value GraphEmitter::emitNodeToEdge(mlir::Value stream, AnchorT anchor, EdgeDirection direction, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef, std::optional<size_t> paramId) {
    auto& graphData = graphs[graphName];
    llvm::SmallVector<mlir::Attribute> nestedParams{columnManager.createRef(graphData.nodeSetColumn)};
    tuples::ColumnRefAttr anchorAttr;
    IdentifierTermAttr identAttr;
    if (std::holds_alternative<IdentifierTermAttr>(anchor)) {
        identAttr = std::get<IdentifierTermAttr>(anchor);
    }
    else {
        anchorAttr = std::get<tuples::ColumnRefAttr>(anchor);
        nestedParams.push_back(anchorAttr);
    }

    auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr(nestedParams));
    auto* block = new mlir::Block();
    block->addArgument(tuples::TupleType::get(ctxt), loc);
    auto nodeSetArg = block->addArgument(graphData.nodeSetColumn->type, loc);
    mlir::BlockArgument anchorArg = anchorAttr ? block->addArgument(anchorAttr.getColumn().type, loc) : mlir::BlockArgument();

    nestedMapOp.getRegion().push_back(block);
    gsubop::NodeRefType nodeRefType;
    tuples::ColumnRefAttr nodeRef;
    {
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointToStart(block);
        mlir::Type identType = identAttr ? mlir::Type(gsubop::IdentifierType::get(ctxt)) : mlir::Type(rewriter.getI32Type());
        auto [identDef, identRef] = createColumn(identType, "idents", "lookup");
        auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& b) -> mlir::Value {
            if (identAttr) {
                auto identOp = b.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, identAttr.getIdent());
                if (paramId) relalg::forwardParameter(op, *paramId, identOp.getOperation());
                return identOp;
            }
            return b.create<variant::ResolveNodeOp>(loc, b.getI32Type(), nodeSetArg, anchorArg);
        });
        scan = filterByIdentifier(scan, identRef);
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
mlir::Value GraphEmitter::emitEdgeToNode(mlir::Value stream, tuples::ColumnRefAttr edgeRef, Member nodeMember, tuples::ColumnRefAttr& rawRef) {
    auto [rawDef, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "raw");
    rawRef = ref;
    return rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, rawDef}}));
}
mlir::Value GraphEmitter::scanEdges(mlir::Value stream, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef) {
    auto& graphData = graphs[graphName];
    auto edgesRef = columnManager.createRef(graphData.edgeSetColumn);
    return scanEdges(stream, edgesRef, graphData.edgeSetColumn->type, edgeRefType, edgeRef);
}
mlir::Value GraphEmitter::scanEdges(mlir::Value stream, tuples::ColumnRefAttr edgesRef, mlir::Type edgeSetType, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef) {
    auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({edgesRef}));
    auto* b = new mlir::Block();
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
mlir::Value GraphEmitter::filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr ref, IdentifierTermAttr ident, std::optional<size_t> paramId) {
    auto identOp = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, ident.getIdent());
    if (paramId) relalg::forwardParameter(op, *paramId, identOp.getOperation());
    return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, identOp);
}
mlir::Value GraphEmitter::filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr identRef) {
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
mlir::Value GraphEmitter::wrapNode(mlir::Value stream, tuples::ColumnRefAttr rawRef, tuples::ColumnDefAttr variantDef) {
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
mlir::Value GraphEmitter::wrapEdge(mlir::Value stream, tuples::ColumnRefAttr edgeRef, tuples::ColumnDefAttr variantDef) {
    // TODO
    // subop::MapCreationHelper helper(ctxt);
    // helper.buildBlock(rewriter, [&](mlir::OpBuilder& b) {
    //     mlir::Value edgeVal = helper.access(edgeRef, loc);
    //     mlir::Value variantVal = b.create<variant::CreateEdgeRefOp>(loc, variant::VariantType::get(ctxt), edgeVal);
    //     b.create<tuples::ReturnOp>(loc, variantVal);
    // });
    // auto mapOp = rewriter.create<subop::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({variantDef}), helper.getColRefs());
    // mapOp.getFn().push_back(helper.getMapBlock());
    // return mapOp.getResult();
    return mlir::Value();
}
mlir::Value GraphEmitter::resolveEdge(mlir::Value stream, tuples::ColumnRefAttr edgeVariantRef, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef) {
    // TODO
    // auto& graphData = graphs[graphName];
    // auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({columnManager.createRef(graphData.edgeSetColumn), edgeVariantRef}));
    // auto* block = new mlir::Block();
    // block->addArgument(tuples::TupleType::get(ctxt), loc);
    // auto edgeSetArg = block->addArgument(graphData.edgeSetColumn->type, loc);
    // auto variantArg = block->addArgument(edgeVariantRef.getColumn().type, loc);
    // nestedMapOp.getRegion().push_back(block);
    // edgeRefType = createEdgeRefType(ctxt, group, graph);
    // {
    //     mlir::OpBuilder::InsertionGuard guard(rewriter);
    //     rewriter.setInsertionPointToStart(block);
    //     auto [idDef, idRef] = createColumn(rewriter.getI32Type(), "edges", "lookup");
    //     auto scan = generateTupleStream(rewriter, loc, idDef, [&](mlir::OpBuilder& b) -> mlir::Value {
    //         return b.create<variant::ResolveEdgeOp>(loc, b.getI32Type(), edgeSetArg, variantArg);
    //     });
    //     scan = filterByIdentifier(scan, idRef);
    //     auto [edgeDef, resolvedRef] = createColumn(edgeRefType, "edges", "ref");
    //     mlir::Value lookup = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), scan, edgeSetArg, rewriter.getArrayAttr({idRef}), edgeDef);
    //     rewriter.create<tuples::ReturnOp>(loc, lookup);
    //     edgeRef = resolvedRef;
    // }
    // return nestedMapOp.getRes();
    return mlir::Value();
}
mlir::Value GraphEmitter::resolveNode(mlir::Value stream, tuples::ColumnRefAttr nodeVariantRef, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef) {
    // TODO
    return mlir::Value();
}
mlir::Value TripleEmitterV2::lower(mlir::Value stream, std::optional<gpm::TripleSlot> anchor) {
    gsubop::EdgeRefType edgeRefType;
    tuples::ColumnRefAttr edgeRef;
    bool anchorSubject = anchor.has_value() ? *anchor == gpm::TripleSlot::subject : mlir::isa<gpm::IdentifierTermAttr>(sTerm);
    bool anchorObject = anchor.has_value() ? *anchor == gpm::TripleSlot::object : !anchorSubject && mlir::isa<gpm::IdentifierTermAttr>(sTerm);
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
    return stream;
}
GraphEmitter::AnchorT TripleEmitterV2::anchorAttr(mlir::Attribute term) {
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
mlir::Value TripleEmitterV2::filterVariantsEqual(mlir::Value stream, tuples::ColumnRefAttr left, tuples::ColumnRefAttr right) {
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
mlir::Value TripleEmitterV2::finish(mlir::Value stream, mlir::Attribute term, tuples::ColumnRefAttr rawRef) {
    const auto* column = &mlir::cast<VariableTermAttr>(term).getProducedBinding().getColumn();
    if (auto it = localTerms.find(column); it != localTerms.end()) {
        auto [tmpDef, tmpRef] = createColumn(variant::VariantType::get(ctxt), "nodes", "tmp");
        stream = wrapNode(stream, rawRef, tmpDef);
        return filterVariantsEqual(stream, tmpRef, it->second);
    }
    auto variantDef = columnManager.createDef(column);
    variantDef.getColumn().type = variant::VariantType::get(ctxt);
    stream = wrapNode(stream, rawRef, variantDef);
    localTerms[column] = columnManager.createRef(column);
    return stream;
}
mlir::Value TripleEmitterV2::emitPredicate(mlir::Value stream, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId) {
    if (auto constPred = mlir::dyn_cast<IdentifierTermAttr>(pTerm)) {
        return filterByIdentifier(stream, edgeRef, constPred, paramId);
    }
    auto nodeRefType = createNodeRefType(ctxt, group, graph);
    auto& graphData = graphs[graphName];
    auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
    auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef, edgeRef}));
    auto* b = new mlir::Block();
    b->addArgument(tuples::TupleType::get(ctxt), loc);
    auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
    auto edgeArg = b->addArgument(edgeRef.getColumn().type, loc);
    nestedMapOp.getRegion().push_back(b);
    auto [rawDef, rawRef] = createColumn(nodeRefType, "nodes", "raw");
    {
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointToStart(b);
        auto identVal = rewriter.create<gsubop::GetIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), edgeArg);
        auto [scanIdentDef, scanIdentRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "scan");
        mlir::Value inner = generateTupleStream(rewriter, loc, scanIdentDef, [&](mlir::OpBuilder&) -> mlir::Value {
            return identVal;
        });
        inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), rawDef);
        rewriter.create<tuples::ReturnOp>(loc, inner);
    }
    return finish(nestedMapOp.getRes(), pTerm, rawRef);
}
mlir::Value TripleEmitterV2::emitTerm(mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId) {
    tuples::ColumnRefAttr rawRef;
    stream = emitEdgeToNode(stream, edgeRef, nodeMember, rawRef);
    if (auto constTerm = mlir::dyn_cast<IdentifierTermAttr>(term)) {
        return filterByIdentifier(stream, rawRef, constTerm, paramId);
    }
    return finish(stream, term, rawRef);
}

mlir::Value TripleEmitter::lower(mlir::Value stream, std::optional<gpm::TripleSlot> anchor) {
    gsubop::EdgeRefType edgeRefType;
    tuples::ColumnRefAttr edgeRef;
    bool anchorSubject = anchor.has_value() ? *anchor == gpm::TripleSlot::subject : mlir::isa<gpm::IdentifierTermAttr>(sTerm);
    bool anchorObject = anchor.has_value() ? *anchor == gpm::TripleSlot::object : !anchorSubject && mlir::isa<gpm::IdentifierTermAttr>(sTerm);
    if (anchorSubject) {
        stream = scanFromAnchor(stream, anchorAttr(sTerm), EdgeDirection::Outgoing, edgeRefType, edgeRef, paramIds[0]);
        stream = emitPredicate(stream, edgeRef, paramIds[1]);
        stream = emitTerm(stream, oTerm, edgeRefType.getToMembers().getMembers()[0], edgeRef, paramIds[2]);
    }
    else if (anchorObject) {
        stream = scanFromAnchor(stream, anchorAttr(oTerm), EdgeDirection::Incoming, edgeRefType, edgeRef, paramIds[2]);
        stream = emitTerm(stream, sTerm, edgeRefType.getFromMembers().getMembers()[0], edgeRef, paramIds[0]);
        stream = emitPredicate(stream, edgeRef, paramIds[1]);
    }
    else {
        auto& graphData = graphs[graphName];
        auto edgesRef = columnManager.createRef(graphData.edgeSetColumn);
        auto edgeSetType = graphData.edgeSetColumn->type;
        stream = scanEdges(stream, edgesRef, edgeSetType, edgeRefType, edgeRef);
        stream = emitTerm(stream, sTerm, edgeRefType.getFromMembers().getMembers()[0], edgeRef, paramIds[0]);
        stream = emitPredicate(stream, edgeRef, paramIds[1]);
        stream = emitTerm(stream, oTerm, edgeRefType.getToMembers().getMembers()[0], edgeRef, paramIds[2]);
    }
    return stream;
}
TripleEmitter::TripleAnchorT TripleEmitter::anchorAttr(mlir::Attribute term) {
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
mlir::Value TripleEmitter::wrapVariant(mlir::Value stream, tuples::ColumnRefAttr rawRef, tuples::ColumnDefAttr variantDef) {
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
        stream = wrapVariant(stream, rawRef, tmpDef);
        return filterVariantsEqual(stream, tmpRef, it->second);
    }
    auto variantDef = columnManager.createDef(column);
    variantDef.getColumn().type = variant::VariantType::get(ctxt);
    stream = wrapVariant(stream, rawRef, variantDef);
    localTerms[column] = columnManager.createRef(column);
    return stream;
}
mlir::Value TripleEmitter::filterValidIdentifier(mlir::Value stream, tuples::ColumnRefAttr identRef) {
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
mlir::Value TripleEmitter::scanFromAnchor(mlir::Value stream, TripleAnchorT anchor, EdgeDirection direction, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef, std::optional<size_t> paramId) {
    auto& graphData = graphs[graphName];
    llvm::SmallVector<mlir::Attribute> nestedParams{columnManager.createRef(graphData.nodeSetColumn)};
    tuples::ColumnRefAttr anchorAttr;
    IdentifierTermAttr identAttr;
    if (std::holds_alternative<IdentifierTermAttr>(anchor)) {
        identAttr = std::get<IdentifierTermAttr>(anchor);
    }
    else {
        anchorAttr = std::get<tuples::ColumnRefAttr>(anchor);
        nestedParams.push_back(anchorAttr);
    }

    auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr(nestedParams));
    auto* block = new mlir::Block();
    block->addArgument(tuples::TupleType::get(ctxt), loc);
    auto nodeSetArg = block->addArgument(graphData.nodeSetColumn->type, loc);
    mlir::BlockArgument anchorArg = anchorAttr ? block->addArgument(anchorAttr.getColumn().type, loc) : mlir::BlockArgument();

    nestedMapOp.getRegion().push_back(block);
    gsubop::NodeRefType nodeRefType;
    tuples::ColumnRefAttr nodeRef;
    {
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointToStart(block);
        mlir::Type identType = identAttr ? mlir::Type(gsubop::IdentifierType::get(ctxt)) : mlir::Type(rewriter.getI32Type());
        auto [identDef, identRef] = createColumn(identType, "idents", "lookup");
        auto scan = generateTupleStream(rewriter, loc, identDef, [&](mlir::OpBuilder& b) -> mlir::Value {
            if (identAttr) {
                auto identOp = b.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, identAttr.getIdent());
                if (paramId) relalg::forwardParameter(op, *paramId, identOp.getOperation());
                return identOp;
            }
            return b.create<variant::ResolveNodeOp>(loc, b.getI32Type(), nodeSetArg, anchorArg);
        });
        scan = filterValidIdentifier(scan, identRef);
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
mlir::Value TripleEmitter::scanEdges(mlir::Value stream, tuples::ColumnRefAttr edgesRef, mlir::Type edgeSetType, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef) {
    auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({edgesRef}));
    auto* b = new mlir::Block();
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
mlir::Value TripleEmitter::emitPredicate(mlir::Value stream, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId) {
    if (auto constPred = mlir::dyn_cast<IdentifierTermAttr>(pTerm)) {
        auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constPred.getIdent());
        if (paramId) relalg::forwardParameter(op, *paramId, ident.getOperation());
        return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, edgeRef, ident);
    }
    auto nodeRefType = createNodeRefType(ctxt, group, graph);
    auto& graphData = graphs[graphName];
    auto nodesRef = columnManager.createRef(graphData.nodeSetColumn);
    auto nestedMapOp = rewriter.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, rewriter.getArrayAttr({nodesRef, edgeRef}));
    auto* b = new mlir::Block();
    b->addArgument(tuples::TupleType::get(ctxt), loc);
    auto nodeSetArg = b->addArgument(graphData.nodeSetColumn->type, loc);
    auto edgeArg = b->addArgument(edgeRef.getColumn().type, loc);
    nestedMapOp.getRegion().push_back(b);
    auto [rawDef, rawRef] = createColumn(nodeRefType, "nodes", "raw");
    {
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointToStart(b);
        auto identVal = rewriter.create<gsubop::GetIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), edgeArg);
        auto [scanIdentDef, scanIdentRef] = createColumn(gsubop::IdentifierType::get(ctxt), "ident", "scan");
        mlir::Value inner = generateTupleStream(rewriter, loc, scanIdentDef, [&](mlir::OpBuilder&) -> mlir::Value {
            return identVal;
        });
        inner = rewriter.create<subop::LookupOp>(loc, tuples::TupleStreamType::get(ctxt), inner, nodeSetArg, rewriter.getArrayAttr({scanIdentRef}), rawDef);
        rewriter.create<tuples::ReturnOp>(loc, inner);
    }
    return finish(nestedMapOp.getRes(), pTerm, rawRef);
}
mlir::Value TripleEmitter::emitTerm(mlir::Value stream, mlir::Attribute term, Member nodeMember, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId) {
    if (auto constTerm = mlir::dyn_cast<IdentifierTermAttr>(term)) {
        auto [def, ref] = createColumn(memberManager.getType(nodeMember), "nodes", "id");
        auto ident = rewriter.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(ctxt), graph, constTerm.getIdent());
        if (paramId) relalg::forwardParameter(op, *paramId, ident.getOperation());
        stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, def}}));
        return rewriter.create<gsubop::FilterByIdentifierOp>(loc, stream, ref, ident);
    }
    auto [rawDef, rawRef] = createColumn(memberManager.getType(nodeMember), "nodes", "raw");
    stream = rewriter.create<subop::GatherOp>(loc, stream, edgeRef, createColumnDefMemberMappingAttr(ctxt, {{nodeMember, rawDef}}));
    return finish(stream, term, rawRef);
}

} // namespace gengodb::compiler::dialect::gpm::lowering