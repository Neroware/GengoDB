#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H

#include "gengodb/compiler/Conversion/GPMToSubOp/helpers.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;
struct NamedGraph {
    gsubop::GraphType graphType;
    tuples::Column* nodeSetColumn;
    tuples::Column* edgeSetColumn;
    mlir::Value externalGraph;
};
using NamedGraphMapper = llvm::DenseMap<mlir::SymbolRefAttr, NamedGraph>;
// One gsubop.get_external_graph per (graph name, global id), shared by all lowerings of the pass.
struct ExternalGraph {
    gsubop::GraphType graphType;
    gsubop::NodeSetType nodeSetType;
    gsubop::EdgeSetType edgeSetType;
    mlir::Value externalGraph;
};
using GraphIdentity = std::pair<mlir::StringAttr, mlir::StringAttr>;
using ExternalGraphMapper = llvm::DenseMap<GraphIdentity, ExternalGraph>;
// Returns the cached external graph state, creating it at the start of `insertionBlock` on first use.
// The block must dominate every later user (i.e. the query's top-level block, not a loop body).
template<typename ColumnAttrT>
ExternalGraph& getOrCreateExternalGraph(mlir::OpBuilder& builder, mlir::Location loc, ColumnAttrT graphAttr, ExternalGraphMapper& externalGraphs, mlir::Block* insertionBlock) {
    auto* ctxt = builder.getContext();
    auto [graphGroup, graphName] = helpers::splitGraphRef(graphAttr);
    auto graphRefType = mlir::cast<gpm::GraphReferenceType>(graphAttr.getColumn().type);
    GraphIdentity identity{graphRefType.getName(), graphRefType.getGlobalId()};
    auto idIt = externalGraphs.find(identity);
    if (idIt == externalGraphs.end()) {
        auto nodeSetType = helpers::createGraphSetType<gsubop::NodeSetType>(ctxt, graphGroup, graphName, "vx");
        auto edgeSetType = helpers::createGraphSetType<gsubop::EdgeSetType>(ctxt, graphGroup, graphName, "ex");
        auto nodeSetMember = helpers::createMember(ctxt, helpers::memberName(graphName, graphGroup, "vx"), nodeSetType);
        auto edgeSetMember = helpers::createMember(ctxt, helpers::memberName(graphName, graphGroup, "ex"), edgeSetType);
        auto graphType = gsubop::GraphType::get(ctxt, helpers::createStateMembersAttr(ctxt, {nodeSetMember}), helpers::createStateMembersAttr(ctxt, {edgeSetMember}));
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(insertionBlock);
        auto externalGraph = builder.create<gsubop::GetExternalGraphOp>(loc, graphType, graphRefType.getName(), graphRefType.getGlobalId());
        idIt = externalGraphs.insert({identity, ExternalGraph{graphType, nodeSetType, edgeSetType, externalGraph}}).first;
    }
    return idIt->second;
}
constexpr llvm::StringLiteral kGraphIdxExpId = "graphIndexExpand";

struct GraphEmitter {
    using AnchorT = std::variant<IdentifierTermAttr, tuples::ColumnRefAttr>;
    enum EdgeDirection { Incoming, Outgoing };

    GraphEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, mlir::Operation* op, tuples::ColumnRefAttr graphRefAttr)
        : rewriter(rewriter), ctxt(rewriter.getContext()), op(op),
        columnManager(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()),
        memberManager(ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager()),
        graphs(graphs), loc(op->getLoc()), graphRefAttr(graphRefAttr), graphName(graphRefAttr.getName()) {
            std::tie(group, graph) = helpers::splitGraphRef(graphRefAttr);
            assert(graphs.count(graphName) && "graph must already be lowered");
    }
    mlir::Value emitNodeToEdge(mlir::Value stream, AnchorT anchor, EdgeDirection direction, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef, std::optional<size_t> paramId = std::nullopt);
    mlir::Value emitEdgeToNode(mlir::Value stream, tuples::ColumnRefAttr edgeRef, subop::Member nodeMember, tuples::ColumnRefAttr& rawRef);
    mlir::Value scanEdges(mlir::Value stream, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef);
    mlir::Value scanEdges(mlir::Value stream, tuples::ColumnRefAttr edgesRef, mlir::Type edgeSetType, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef);
    mlir::Value filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr ref, IdentifierTermAttr ident, std::optional<size_t> paramId = std::nullopt);
    mlir::Value filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr identRef);
    mlir::Value wrapNode(mlir::Value stream, tuples::ColumnRefAttr rawRef, tuples::ColumnDefAttr variantDef);
    mlir::Value wrapEdge(mlir::Value stream, tuples::ColumnRefAttr edgeRef, tuples::ColumnDefAttr variantDef);
    mlir::Value resolveNode(mlir::Value stream, tuples::ColumnRefAttr nodeVariantRef, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef);
    mlir::Value resolveEdge(mlir::Value stream, tuples::ColumnRefAttr edgeVariantRef, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef);
    void setUseEdgeWeights(bool useEdgeWeights) { this->useEdgeWeights = useEdgeWeights; }

    protected:
    mlir::ConversionPatternRewriter& rewriter;
    mlir::MLIRContext* ctxt;
    mlir::Operation* op;
    tuples::ColumnManager& columnManager;
    subop::MemberManager& memberManager;
    NamedGraphMapper& graphs;
    mlir::Location loc;
    tuples::ColumnRefAttr graphRefAttr;
    mlir::SymbolRefAttr graphName;
    std::string group, graph;
    bool useEdgeWeights = false;
}; // GraphEmitter

// Lowers a triple over a tuplestream that carries a graph's node/edge set columns (see NamedGraphMapper).
class TripleEmitter : public GraphEmitter {
    mlir::Attribute sTerm, pTerm, oTerm;
    tuples::ColumnDefAttr truthDegrAttr;
    llvm::DenseMap<const tuples::Column*, tuples::ColumnRefAttr> localTerms;
    std::array<std::optional<size_t>, 3> paramIds;

    public:
    template<typename OpTy>
    TripleEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, OpTy op)
        : GraphEmitter(rewriter, graphs, op.getOperation(), op.getGraphRef()),
        sTerm(op.getS()), pTerm(op.getP()), oTerm(op.getO()), truthDegrAttr(op.getTruthDegrAttr()),
        paramIds{op.getParamId(TripleSlot::subject), op.getParamId(TripleSlot::predicate), op.getParamId(TripleSlot::object)} {}
    TripleEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, mlir::Operation* op, mlir::DictionaryAttr triple);
    void bind(tuples::ColumnRefAttr anchorRef) {
        bind(&anchorRef.getColumn(), anchorRef);
    }
    void bind(const tuples::Column* tripleColumn, tuples::ColumnRefAttr boundRef) {
        localTerms[tripleColumn] = boundRef;
    }
    mlir::Value lower(mlir::Value stream, std::optional<TripleSlot> anchor = std::nullopt);

    private:
    AnchorT anchorAttr(mlir::Attribute term);
    mlir::Value filterVariantsEqual(mlir::Value stream, tuples::ColumnRefAttr left, tuples::ColumnRefAttr right);
    mlir::Value finish(mlir::Value stream, mlir::Attribute term, tuples::ColumnRefAttr rawRef);
    mlir::Value emitPredicate(mlir::Value stream, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId = std::nullopt);
    mlir::Value emitTerm(mlir::Value stream, mlir::Attribute term, subop::Member nodeMember, tuples::ColumnRefAttr edgeRef, std::optional<size_t> paramId = std::nullopt);
    mlir::Value emitTruthDegree(mlir::Value stream, gsubop::EdgeRefType edgeRefType, tuples::ColumnRefAttr edgeRef);
}; // TripleEmitter

} // namespace gengodb::compiler::dialect::gpm::lowering

#endif // GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H