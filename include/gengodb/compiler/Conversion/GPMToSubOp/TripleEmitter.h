#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H

#include "gengodb/compiler/Conversion/GPMToSubOp/helpers.h"

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;
struct NamedGraph {
    gsubop::GraphType graphType;
    tuples::Column* nodeSetColumn;
    tuples::Column* edgeSetColumn;
    mlir::Value externalGraph;
};
using NamedGraphMapper = llvm::DenseMap<mlir::SymbolRefAttr, NamedGraph>;

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
}; // GraphEmitter

// Lowers a triple over a tuplestream that carries a graph's node/edge set columns (see NamedGraphMapper).
class TripleEmitter : public GraphEmitter {
    mlir::Attribute sTerm, pTerm, oTerm;
    llvm::DenseMap<const tuples::Column*, tuples::ColumnRefAttr> localTerms;
    std::array<std::optional<size_t>, 3> paramIds;

    public:
    template<typename OpTy>
    TripleEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, OpTy op)
        : GraphEmitter(rewriter, graphs, op.getOperation(), op.getGraphRef()),
        sTerm(op.getS()), pTerm(op.getP()), oTerm(op.getO()),
        paramIds{op.getParamId(TripleSlot::subject), op.getParamId(TripleSlot::predicate), op.getParamId(TripleSlot::object)} {}
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
}; // TripleEmitter

} // namespace gengodb::compiler::dialect::gpm::lowering

#endif // GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H