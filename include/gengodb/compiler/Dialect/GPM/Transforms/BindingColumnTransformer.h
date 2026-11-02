#ifndef GENGODB_COMPILER_DIALECT_GPM_TRANSFORMS_BINDINGCOLUMNTRANSFORMER_H
#define GENGODB_COMPILER_DIALECT_GPM_TRANSFORMS_BINDINGCOLUMNTRANSFORMER_H
#include "lingodb/compiler/Dialect/TupleStream/Column.h"
#include "lingodb/compiler/Dialect/TupleStream/ColumnManager.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamDialect.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
namespace gengodb::compiler::dialect::gpm {
using namespace lingodb::compiler::dialect;
using ColumnMapper = llvm::DenseMap<const lingodb::compiler::dialect::tuples::Column*, const lingodb::compiler::dialect::tuples::Column*>;
class BindingColumnTransformer {
    tuples::ColumnManager& columnManager;
    public:
    BindingColumnTransformer(mlir::MLIRContext* context) 
        : columnManager(context->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()) {}
    mlir::Attribute remapColumnAttr(mlir::Attribute attr, const ColumnMapper& colMap);
    void remapColumnsOf(mlir::Operation* op, const ColumnMapper& colMap);
    void remapColumnsIn(const llvm::SmallPtrSetImpl<mlir::Operation*>& ops, const ColumnMapper& colMap);
    void remapColumnsEverywhere(mlir::Operation* op, mlir::Operation* skip, const ColumnMapper& colMap, const llvm::SmallPtrSetImpl<mlir::Operation*>* exclude = nullptr);
}; // END class BindingColumnTransformer

} // namespace gengodb::compiler::dialect::gpm
#endif //GENGODB_COMPILER_DIALECT_GPM_TRANSFORMS_BINDINGCOLUMNTRANSFORMER_H
