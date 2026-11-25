#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H

#include "gengodb/compiler/Conversion/GPMToSubOp/helpers.h"

// #include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
// #include "gengodb/compiler/Dialect/Variant/VariantOps.h"
// #include "gengodb/semantics/Datatypes.h"
// #include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
// #include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
// #include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
// #include "lingodb/compiler/Dialect/SubOperator/Utils.h"

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;
struct NamedGraph {
    gsubop::GraphType graphType;
    tuples::Column* nodeSetColumn;
    tuples::Column* edgeSetColumn;
    mlir::Value externalGraph;
};
using NamedGraphMapper = llvm::DenseMap<mlir::SymbolRefAttr, NamedGraph>;
constexpr llvm::StringLiteral kGraphIdxExpId = "graphIndexExpand";

struct GraphHelper {
    GraphHelper(mlir::OpBuilder& builder, mlir::Location loc)
        : builder(builder), loc(loc), ctxt(builder.getContext()), 
        columnManager(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()) {}
    inline gsubop::CreateIdentifierOp createIdentifier(const std::string& graph, gpm::IdentifierTermAttr ident, std::optional<size_t> paramId) {
        auto identOp = builder.create<gsubop::CreateIdentifierOp>(loc, gsubop::IdentifierType::get(builder.getContext()), graph, ident.getIdent());
        if (paramId) identOp->setAttr(relalg::kQueryParamsAttrName, relalg::makeQueryParamsAttr(builder.getContext(), *paramId, ident.getIdent()));
        return identOp;
    }
    inline mlir::Value attach(mlir::Value stream, llvm::ArrayRef<tuples::ColumnRefAttr> columns, llvm::function_ref<mlir::Value(mlir::Value, mlir::ValueRange)> buildInner) {
        llvm::SmallVector<mlir::Attribute> refs(columns.begin(), columns.end());
        auto nestedMapOp = builder.create<subop::NestedMapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, builder.getArrayAttr(refs));
        auto* block = new mlir::Block;
        auto tuple = block->addArgument(tuples::TupleType::get(ctxt), loc);
        for (auto ref : columns) block->addArgument(ref.getColumn().type, loc);
        nestedMapOp.getRegion().push_back(block);
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(block);
        builder.create<tuples::ReturnOp>(loc, buildInner(tuple, block->getArguments().drop_front()));
        return nestedMapOp.getRes();
    }
    inline mlir::Value attach(mlir::Value graphStream, const NamedGraph& graph, llvm::function_ref<mlir::Value(mlir::Value, mlir::Value, mlir::Value)> buildInner) {
        return attach(graphStream, {columnManager.createRef(graph.nodeSetColumn), columnManager.createRef(graph.edgeSetColumn)},
            [&](mlir::Value tuple, mlir::ValueRange args) { return buildInner(tuple, args[0], args[1]); });
    }
    inline mlir::Value nodeId(mlir::Value stream, tuples::ColumnRefAttr nodeRef, gsubop::NodeRefType nodeRefType, tuples::ColumnRefAttr& idRef) {
        auto [idDef, ref] = helpers::createColumn(builder.getI32Type(), "path", "id");
        idRef = ref;
        return builder.create<subop::GatherOp>(loc, stream, nodeRef, helpers::createColumnDefMemberMappingAttr(ctxt, {{nodeRefType.getNodeMembers().getMembers()[0], idDef}}));
    }

    private:
    mlir::OpBuilder& builder;
    mlir::Location loc;
    mlir::MLIRContext* ctxt;
    tuples::ColumnManager& columnManager;
};


struct GraphPatternHelper {
    using AnchorT = std::variant<IdentifierTermAttr, tuples::ColumnRefAttr>;
    enum EdgeDirection { Incoming, Outgoing };

    GraphPatternHelper(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, mlir::Operation* op, tuples::ColumnRefAttr graphRefAttr)
        : rewriter(rewriter), ctxt(rewriter.getContext()), op(op),
        columnManager(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()),
        memberManager(ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager()),
        graphs(graphs), loc(op->getLoc()), graphHelper(rewriter, loc), graphRefAttr(graphRefAttr), graphName(graphRefAttr.getName()) {
            std::tie(group, graph) = helpers::splitGraphRef(graphRefAttr);
            assert(graphs.count(graphName) && "graph must already be lowered");
    }
    mlir::Value emitNodeToEdge(mlir::Value stream, AnchorT anchor, EdgeDirection direction, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef, std::optional<size_t> paramId = std::nullopt);
    mlir::Value emitEdgeToNode(mlir::Value stream, tuples::ColumnRefAttr edgeRef, subop::Member nodeMember, tuples::ColumnRefAttr& rawRef);
    mlir::Value scanEdges(mlir::Value stream, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef);
    mlir::Value scanEdges(mlir::Value stream, tuples::ColumnRefAttr edgesRef, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRefColumnRef);
    mlir::Value filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr ref, IdentifierTermAttr ident, std::optional<size_t> paramId = std::nullopt);
    mlir::Value filterByIdentifier(mlir::Value stream, tuples::ColumnRefAttr identRef);
    mlir::Value wrapNode(mlir::Value stream, tuples::ColumnRefAttr rawRef, tuples::ColumnDefAttr variantDef);
    mlir::Value wrapEdge(mlir::Value stream, tuples::ColumnRefAttr edgeRef, tuples::ColumnDefAttr variantDef);
    mlir::Value resolveNode(mlir::Value stream, tuples::ColumnRefAttr nodeVariantRef, gsubop::NodeRefType& nodeRefType, tuples::ColumnRefAttr& nodeRef);
    mlir::Value resolveEdge(mlir::Value stream, tuples::ColumnRefAttr edgeVariantRef, gsubop::EdgeRefType& edgeRefType, tuples::ColumnRefAttr& edgeRef);

    void setUseEdgeWeights(bool useEdgeWeights) { this->useEdgeWeights = useEdgeWeights; }
    std::pair<std::string, std::string> getGraphName() const { return {group, graph}; }
    NamedGraph& getGraph() const { return graphs[graphName]; }

    protected:
    mlir::ConversionPatternRewriter& rewriter;
    mlir::MLIRContext* ctxt;
    mlir::Operation* op;
    tuples::ColumnManager& columnManager;
    subop::MemberManager& memberManager;
    NamedGraphMapper& graphs;
    mlir::Location loc;
    GraphHelper graphHelper;
    tuples::ColumnRefAttr graphRefAttr;
    mlir::SymbolRefAttr graphName;
    std::string group, graph;
    bool useEdgeWeights = false;
}; // GraphPatternHelper

// Lowers a triple over a tuplestream that carries a graph's node/edge set columns (see NamedGraphMapper).
class TripleEmitter : public GraphPatternHelper {
    mlir::Attribute sTerm, pTerm, oTerm;
    tuples::ColumnDefAttr truthDegrAttr;
    llvm::DenseMap<const tuples::Column*, tuples::ColumnRefAttr> localTerms;
    std::array<std::optional<size_t>, 3> paramIds;

    public:
    template<typename OpTy>
    TripleEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& graphs, OpTy op)
        : GraphPatternHelper(rewriter, graphs, op.getOperation(), op.getGraphRef()),
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

class PathPatternEmitter : public GraphPatternHelper {
    public:
    PathPatternEmitter(mlir::ConversionPatternRewriter& rewriter, NamedGraphMapper& localGraphs, mlir::Operation* op, tuples::ColumnRefAttr graphRef)
        : GraphPatternHelper(rewriter, localGraphs, op, graphRef) {}

    // a single step from node id `fromRef` along the step edges: -> i32 id of the neighbor
    // mlir::Value step(mlir::Value stream, tuples::ColumnRefAttr fromRef, bool outgoing, llvm::ArrayRef<mlir::Attribute> predicates, tuples::ColumnRefAttr& toRef);

    void addAnchor(mlir::Value anchor, tuples::ColumnRefAttr idRef) { anchors.push_back({anchor, idRef}); }
    mlir::Value emitFrontier();
    mlir::Value emitVisited();
    mlir::Value emitStep(mlir::Value frontier);
    mlir::Value finish(mlir::Value stream);

    private:
    llvm::SmallVector<std::pair<mlir::Value, tuples::ColumnRefAttr>> anchors;
}; // PathPatternEmitter

} // namespace gengodb::compiler::dialect::gpm::lowering

#endif // GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_TRIPLEEMITTER_H