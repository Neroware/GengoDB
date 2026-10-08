#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include "llvm/ADT/TypeSwitch.h"

#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OpImplementation.h"

#include <array>
#include <optional>

using operator_list = llvm::SmallVector<GPMOperator, 4>;
namespace {
using namespace lingodb::compiler::dialect;
using namespace relalg;
using namespace tuples;

operator_list getChildOperators(mlir::Operation* parent) {
    operator_list children;
    for (auto operand : parent->getOperands()) {
        if (auto childOperator = mlir::dyn_cast_or_null<GPMOperator>(operand.getDefiningOp())) {
            children.push_back(childOperator);
        }
    }
    return children;
}
ColumnSet collectColumns(operator_list operators, std::function<relalg::ColumnSet(GPMOperator)> fn) {
    ColumnSet collected;
    for (auto op : operators) {
        auto res = fn(op);
        collected.insert(res);
    }
    return collected;
}

} // namespace

namespace gengodb::compiler::dialect::gpm::detail {
using namespace lingodb::compiler::dialect::relalg;
inline auto filterVariableTerms(mlir::Operation* op, bool isBound) {
    return llvm::make_filter_range(
        op->getAttrs(), [isBound](mlir::NamedAttribute attr) {
            auto v = mlir::dyn_cast<VariableTermAttr>(attr.getValue());
            return v && (v.hasBinding() == isBound);
        });
}
ColumnSet getCreatedVariables(mlir::Operation* op) {
    ColumnSet columns;
    for (auto x : filterVariableTerms(op, false)) {
        columns.insert(mlir::cast<VariableTermAttr>(x.getValue())
            .getProducedBinding().getColumnPtr().get());
    }
    return columns;
}
ColumnSet getBoundVariables(mlir::Operation* op) {
    ColumnSet columns;
    for (auto x : filterVariableTerms(op, true)) {
        columns.insert(mlir::cast<VariableTermAttr>(x.getValue())
            .getBindingReference().getColumnPtr().get());
    }
    return columns;
}
ColumnSet getUsedVariables(mlir::Operation* op) {
    return getBoundVariables(op)
        .insert(getCreatedVariables(op));
}
ColumnSet getAvailableVariables(mlir::Operation* op) {
   GPMOperator asOperator = mlir::dyn_cast_or_null<GPMOperator>(op);
   auto collected = collectColumns(getChildOperators(op), [](GPMOperator op) { return op.getAvailableVariables(); });
   auto selfCreated = asOperator.getCreatedVariables();
   collected.insert(selfCreated);
   return collected;
}
void moveSubTreeBefore(mlir::Operation* op, mlir::Operation* before) {
    auto tree = mlir::dyn_cast_or_null<Operator>(op);
    if (tree->isBeforeInBlock(before)) {
        return;
    }
    tree->moveBefore(before);
    for (auto child : tree.getChildren()) {
        moveSubTreeBefore(child, tree);
    }
}

llvm::SmallVector<std::tuple<mlir::Attribute, mlir::Attribute, mlir::Attribute>, 16> getPatternTriples(mlir::Operation* op) {
    llvm::SmallVector<std::tuple<mlir::Attribute, mlir::Attribute, mlir::Attribute>, 16> result;
    auto patternOp = mlir::cast<GraphPatternOp>(op);
    patternOp.getPattern().walk([&](gengodb::compiler::dialect::gpm::TriplePatternOp triple) {
        result.push_back(std::make_tuple(triple.getS(), triple.getP(), triple.getO()));
    });
    return result;
}

inline bool isAllowedGraphPatternBodyOp(const mlir::Operation& nested) {
    return mlir::isa<gengodb::compiler::dialect::gpm::TriplePatternOp,
        gengodb::compiler::dialect::gpm::BasicGraphPatternOp,
        gengodb::compiler::dialect::gpm::OptionalGraphPatternOp,
        gengodb::compiler::dialect::gpm::BagOp,
        gengodb::compiler::dialect::gpm::FilterOp,
        tuples::ReturnOp>(nested);
}

mlir::LogicalResult verifyGraphPatternBody(mlir::Operation* op) {
    auto patternOp = mlir::cast<GraphPatternOp>(op);
    bool valid = std::all_of(patternOp.getPattern().getOps().begin(), patternOp.getPattern().getOps().end(), isAllowedGraphPatternBodyOp);
    if (!valid) {
        return op->emitOpError("A graph pattern must only contain triples, nested graph patterns, or FILTER expressions.");
    }
    return mlir::success();
}

mlir::LogicalResult verifyUnionMapping(mlir::Operation* op, mlir::ArrayAttr mapping) {
    for (mlir::Attribute attr : mapping) {
        auto colDef = mlir::dyn_cast<ColumnDefAttr>(attr);
        if (!colDef) {
            return op->emitOpError("mapping entries must be column definitions");
        }
        auto fromExisting = mlir::dyn_cast_or_null<mlir::ArrayAttr>(colDef.getFromExisting());
        if (!fromExisting || fromExisting.size() != 2) {
            return op->emitOpError("every mapping entry must be defined from exactly two (left, right) sources");
        }
        unsigned unitSides = 0;
        for (mlir::Attribute side : fromExisting) {
            if (mlir::isa<mlir::UnitAttr>(side)) {
                unitSides++;
            } 
            else if (!mlir::isa<ColumnRefAttr>(side)) {
                return op->emitOpError("a mapping entry's source must be a column reference or unit");
            }
        }
        if (unitSides == 2) {
            return op->emitOpError("a mapping entry must be bound by at least one branch");
        }
        if (unitSides == 1 && !mlir::isa<lingodb::compiler::dialect::db::NullableType>(colDef.getColumn().type)) {
            return op->emitOpError("a mapping entry bound by only one branch must be nullable");
        }
    }
    return mlir::success();
}

} // namespace gengodb::compiler::dialect::gpm::detail

namespace gengodb::compiler::dialect {

lingodb::compiler::dialect::relalg::ColumnSet gpm::BasicGraphPatternOp::getCreatedVariables() {
    lingodb::compiler::dialect::relalg::ColumnSet res;
    getPattern().walk([&](TriplePatternOp triple){
        res.insert(triple.getCreatedVariables());
    });
    return res;
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::BasicGraphPatternOp::getUsedVariables() {
    lingodb::compiler::dialect::relalg::ColumnSet res;
    getPattern().walk([&](TriplePatternOp triple){
        res.insert(triple.getUsedVariables());
    });
    return res;
}

lingodb::compiler::dialect::relalg::ColumnSet gpm::TriplePatternOp::getCreatedColumns() {
    return getCreatedVariables();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::TriplePatternOp::getUsedColumns() {
    return getBoundVariables();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::TriplePatternOp::getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache) {
    lingodb::compiler::dialect::relalg::ColumnSet available;
    if (auto child = mlir::dyn_cast_or_null<Operator>(getRel().getDefiningOp())) {
        available.insert(cache.getAvailableColumnsFor(child));
    }
    available.insert(getCreatedColumns());
    return available;
}
bool gpm::TriplePatternOp::canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column) {
    return lingodb::compiler::dialect::relalg::detail::canColumnReach(getOperation(), source, target, column);
}
namespace {
std::vector<relalg::QueryParamLiteral> paramLiterals(std::array<mlir::Attribute, 3> terms) {
    std::vector<relalg::QueryParamLiteral> literals;
    for (mlir::Attribute term : terms) {
        if (auto ident = mlir::dyn_cast<gpm::IdentifierTermAttr>(term)) {
            literals.push_back({ident.getIdent(), ident.getIdent().getType()});
        }
    }
    return literals;
}
std::optional<size_t> paramIdForSlot(mlir::Operation* op, std::array<mlir::Attribute, 3> terms, unsigned slotIndex) {
    auto paramsAttr = op->getAttrOfType<mlir::ArrayAttr>(relalg::kQueryParamsAttrName);
    if (!paramsAttr) return std::nullopt;
    if (!mlir::isa<gpm::IdentifierTermAttr>(terms[slotIndex])) return std::nullopt;
    size_t entryIdx = 0;
    for (unsigned i = 0; i < slotIndex; ++i) {
        if (mlir::isa<gpm::IdentifierTermAttr>(terms[i])) ++entryIdx;
    }
    if (entryIdx >= paramsAttr.size()) return std::nullopt;
    auto dict = mlir::cast<mlir::DictionaryAttr>(paramsAttr[entryIdx]);
    return static_cast<size_t>(mlir::cast<mlir::IntegerAttr>(dict.get(relalg::kQueryParamIdKey)).getInt());
}
template <class OpTy>
void maskIdentifierTerms(OpTy op) {
    auto placeholder = gpm::IdentifierTermAttr::get(op.getContext(), mlir::StringAttr::get(op.getContext(), ""));
    if (mlir::isa<gpm::IdentifierTermAttr>(op.getS())) op.setSAttr(placeholder);
    if (mlir::isa<gpm::IdentifierTermAttr>(op.getP())) op.setPAttr(placeholder);
    if (mlir::isa<gpm::IdentifierTermAttr>(op.getO())) op.setOAttr(placeholder);
}
} // namespace
std::vector<relalg::QueryParamLiteral> gpm::TriplePatternOp::getParamLiterals() {
    return paramLiterals({getS(), getP(), getO()});
}
std::optional<size_t> gpm::TriplePatternOp::getParamId(TripleSlot slot) {
    return paramIdForSlot(getOperation(), {getS(), getP(), getO()}, static_cast<unsigned>(slot));
}
void gpm::TriplePatternOp::maskParameters() {
    maskIdentifierTerms(*this);
}

lingodb::compiler::dialect::relalg::ColumnSet gpm::GraphExpansionOp::getCreatedColumns() {
    return getCreatedVariables();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::GraphExpansionOp::getUsedColumns() {
    return getBoundVariables();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::GraphExpansionOp::getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache) {
    lingodb::compiler::dialect::relalg::ColumnSet columns;
    if (auto child = mlir::dyn_cast_or_null<Operator>(getRel().getDefiningOp())) {
        columns.insert(cache.getAvailableColumnsFor(child));
    }
    columns.insert(getCreatedColumns());
    return columns;
}
bool gpm::GraphExpansionOp::canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column) {
    return lingodb::compiler::dialect::relalg::detail::canColumnReach(getOperation(), source, target, column);
}
std::vector<relalg::QueryParamLiteral> gpm::GraphExpansionOp::getParamLiterals() {
    return paramLiterals({getS(), getP(), getO()});
}
std::optional<size_t> gpm::GraphExpansionOp::getParamId(TripleSlot slot) {
    return paramIdForSlot(getOperation(), {getS(), getP(), getO()}, static_cast<unsigned>(slot));
}
void gpm::GraphExpansionOp::maskParameters() {
    maskIdentifierTerms(*this);
}

bool gpm::BindingsCompatibleOp::isEqualityPred(bool nullsAreEqual) {
    return nullsAreEqual || (!mlir::isa<db::NullableType>(getLhs().getType()) 
        && !mlir::isa<db::NullableType>(getRhs().getType()));
}
bool gpm::BindingsCompatibleOp::isBindingCompatiblePred() {
    return true;
}
bool gpm::BindingsCompatibleOp::isUnequalityPred() {
    return false;
}
bool gpm::BindingsCompatibleOp::isLessPred(bool eq) {
    return false;
}
bool gpm::BindingsCompatibleOp::isGreaterPred(bool eq) {
    return false;
}
mlir::Value gpm::BindingsCompatibleOp::getLeft() {
    return getLhs();
}
mlir::Value gpm::BindingsCompatibleOp::getRight() {
    return getRhs();
}

lingodb::compiler::dialect::relalg::ColumnSet gpm::NamedGraphOp::getCreatedColumns() {
    return lingodb::compiler::dialect::relalg::ColumnSet();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::NamedGraphOp::getUsedColumns() {
    return lingodb::compiler::dialect::relalg::ColumnSet();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::NamedGraphOp::getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache&) {
    return getCreatedColumns();
}
bool gpm::NamedGraphOp::canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column) {
    return lingodb::compiler::dialect::relalg::detail::canColumnReach(getOperation(), source, target, column);
}

lingodb::compiler::dialect::relalg::ColumnSet gpm::BasicGraphPatternOp::getCreatedColumns() {
    return getCreatedVariables();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::BasicGraphPatternOp::getUsedColumns() {
    lingodb::compiler::dialect::relalg::ColumnSet used;
    getPattern().walk([&](TriplePatternOp triple){
        used.insert(triple.getBoundVariables());
    });
    return used;
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::BasicGraphPatternOp::getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache) {
    lingodb::compiler::dialect::relalg::ColumnSet available;
    if (auto child = mlir::dyn_cast_or_null<Operator>(getRel().getDefiningOp())) {
        available.insert(cache.getAvailableColumnsFor(child));
    }
    available.insert(getCreatedColumns());
    return available;
}
bool gpm::BasicGraphPatternOp::canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column) {
    return lingodb::compiler::dialect::relalg::detail::canColumnReach(getOperation(), source, target, column);
}

lingodb::compiler::dialect::relalg::ColumnSet gpm::OptionalGraphPatternOp::getCreatedVariables() {
    lingodb::compiler::dialect::relalg::ColumnSet res;
    getPattern().walk([&](TriplePatternOp triple){
        res.insert(triple.getCreatedVariables());
    });
    return res;
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::OptionalGraphPatternOp::getUsedVariables() {
    lingodb::compiler::dialect::relalg::ColumnSet res;
    getPattern().walk([&](TriplePatternOp triple){
        res.insert(triple.getUsedVariables());
    });
    return res;
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::OptionalGraphPatternOp::getCreatedColumns() {
    return getCreatedVariables();
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::OptionalGraphPatternOp::getUsedColumns() {
    lingodb::compiler::dialect::relalg::ColumnSet used;
    getPattern().walk([&](TriplePatternOp triple){
        used.insert(triple.getBoundVariables());
    });
    return used;
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::OptionalGraphPatternOp::getAvailableColumns(lingodb::compiler::dialect::relalg::AvailabilityCache& cache) {
    lingodb::compiler::dialect::relalg::ColumnSet available;
    if (auto child = mlir::dyn_cast_or_null<Operator>(getRel().getDefiningOp())) {
        available.insert(cache.getAvailableColumnsFor(child));
    }
    available.insert(getCreatedColumns());
    return available;
}
bool gpm::OptionalGraphPatternOp::canColumnReach(Operator source, Operator target, const lingodb::compiler::dialect::tuples::Column* column) {
    return lingodb::compiler::dialect::relalg::detail::canColumnReach(getOperation(), source, target, column);
}

lingodb::compiler::dialect::relalg::ColumnSet gpm::BagOp::getCreatedVariables() {
    lingodb::compiler::dialect::relalg::ColumnSet res;
    for (auto attr : getMapping()) {
        res.insert(mlir::cast<tuples::ColumnDefAttr>(attr).getColumnPtr().get());
    }
    return res;
}
lingodb::compiler::dialect::relalg::ColumnSet gpm::BagOp::getUsedVariables() {
    lingodb::compiler::dialect::relalg::ColumnSet used;
    for (auto attr : getMapping()) {
        auto fromExisting = mlir::cast<mlir::ArrayAttr>(mlir::cast<tuples::ColumnDefAttr>(attr).getFromExisting());
        for (auto side : fromExisting) {
            if (auto ref = mlir::dyn_cast<tuples::ColumnRefAttr>(side)) {
                used.insert(ref.getColumnPtr().get());
            }
        }
    }
    return used;
}

} // namespace gengodb::compiler::dialect



#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsInterfaces.cpp.inc"