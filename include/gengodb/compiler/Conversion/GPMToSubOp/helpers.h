#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_HELPERS_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_HELPERS_H

#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamDialect.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"

#include "mlir/Transforms/DialectConversion.h"

namespace gengodb::compiler::dialect::gpm::lowering {
using namespace lingodb::compiler::dialect;

namespace helpers {
template<typename ColumnAttrT>
std::pair<std::string, std::string> splitGraphRef(ColumnAttrT columnAttr) {
    auto refType = mlir::cast<GraphReferenceType>(columnAttr.getColumn().type);
    return {columnAttr.getName().getRootReference().str(), refType.getName().str()};
}
inline tuples::ColumnDefAttr createDef(tuples::ColumnManager& columnManager, std::string scope, std::string name, mlir::Type type, bool uniqueScope = true) {
    scope = uniqueScope ? columnManager.getUniqueScope(scope) : scope;
    auto def = columnManager.createDef(scope, name);
    def.getColumn().type = type;
    return def;
}
inline tuples::ColumnRefAttr createRef(tuples::ColumnManager& columnManager, std::string scope, std::string name) {
    auto col = columnManager.get(scope, name);
    return columnManager.createRef(col.get());
}
inline std::pair<tuples::ColumnDefAttr, tuples::ColumnRefAttr> createColumn(mlir::Type type, std::string scope, std::string name) {
    auto& columnManager = type.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
    auto def = createDef(columnManager, scope, name, type);
    auto ref = createRef(columnManager, def.getName().getRootReference().str(), def.getName().getLeafReference().str());
    return {def, ref};
}
inline subop::Member createMember(mlir::MLIRContext* context, std::string name, mlir::Type type) {
    auto& memberManager = context->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager();
    return memberManager.createMember(name, type);
}
inline subop::StateMembersAttr createStateMembersAttr(mlir::MLIRContext* context, const llvm::SmallVector<Member>& members) {
    return subop::StateMembersAttr::get(context, members);
}
inline subop::ColumnDefMemberMappingAttr createColumnDefMemberMappingAttr(mlir::MLIRContext* context, llvm::SmallVector<subop::DefMappingPairT> pairs) {
    return subop::ColumnDefMemberMappingAttr::get(context, pairs);
}
inline std::string memberName(std::string name, std::string prefix, std::string suffix) {
    return (suffix.empty() ? "" : prefix + "_") + name + (suffix.empty() ? "" : "_" + suffix);
}
template<typename GSetType>
GSetType createGraphSetType(mlir::MLIRContext* ctxt, std::string group, std::string graph, std::string name, std::string itStrategy = "all") {
    auto itType = gsubop::GraphSetIteratorType::get(ctxt, mlir::ArrayAttr::get(ctxt, {mlir::StringAttr::get(ctxt, itStrategy)}));
    auto itMember = createMember(ctxt, memberName(graph, group, name + "_it"), itType);
    return GSetType::get(ctxt, createStateMembersAttr(ctxt, {itMember}));
}
inline gsubop::NodeRefType createNodeRefType(mlir::MLIRContext* ctxt, std::string group, std::string graph) {
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
inline gsubop::EdgeRefType createEdgeRefType(mlir::MLIRContext* ctxt, std::string group, std::string graph) {
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

} // helpers
} // end namespace gengodb::compiler::dialect::gpm::lowering

#endif // GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_HELPERS_H