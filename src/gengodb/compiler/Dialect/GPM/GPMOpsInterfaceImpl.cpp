#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include "llvm/ADT/TypeSwitch.h"

#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OpImplementation.h"

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
      op->getAttrs(), [&](mlir::NamedAttribute attr) {
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

} // namespace gengodb::compiler::dialect



#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsInterfaces.cpp.inc"