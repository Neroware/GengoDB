#ifndef GENGODB_COMPILER_CONVERSION_GRAPHSUBOPTOCF_GRAPHENTRYTYPES_H
#define GENGODB_COMPILER_CONVERSION_GRAPHSUBOPTOCF_GRAPHENTRYTYPES_H

#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Transforms/DialectConversion.h"

namespace gengodb::compiler::dialect {

template <typename StorageHelper>
inline mlir::TupleType getNodeEntryType(gsubop::NodeRefType t, mlir::TypeConverter& converter) {
   auto i1Type = mlir::IntegerType::get(t.getContext(), 1);
   auto i8Type = mlir::IntegerType::get(t.getContext(), 8);
   auto i32Type = mlir::IntegerType::get(t.getContext(), 32);
   auto propertyTupleType = StorageHelper(nullptr, t.getPropertyMembers(), false, &converter).getStorageType();
   return mlir::TupleType::get(t.getContext(), {i32Type, i1Type, i8Type, i8Type, i8Type, i8Type, i8Type, i8Type, propertyTupleType});
}

template <typename StorageHelper>
inline mlir::TupleType getEdgeEntryType(gsubop::EdgeRefType t, mlir::TypeConverter& converter) {
   auto i1Type = mlir::IntegerType::get(t.getContext(), 1);
   auto i8Type = mlir::IntegerType::get(t.getContext(), 8);
   auto i32Type = mlir::IntegerType::get(t.getContext(), 32);
   auto propertyTupleType = StorageHelper(nullptr, t.getPropertyMembers(), false, &converter).getStorageType();
   return mlir::TupleType::get(t.getContext(), {i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i1Type, i8Type, propertyTupleType});
}

template <typename StorageHelper>
inline mlir::TupleType getPropertyType(gsubop::TypedPropertyRefType t, mlir::TypeConverter& converter) {
   auto valTupleType = StorageHelper(nullptr, t.getMembers(), false, &converter).getStorageType();
   return mlir::cast<mlir::TupleType>(converter.convertType(valTupleType));
}

inline mlir::TupleType getPropertyEntryType(gsubop::PropertyRefType t, mlir::TypeConverter& /*converter*/) {
   auto i1Type = mlir::IntegerType::get(t.getContext(), 1);
   auto i32Type = mlir::IntegerType::get(t.getContext(), 32);
   return mlir::TupleType::get(t.getContext(), {i32Type, i32Type, i32Type, i32Type, i32Type, i1Type});
}

inline mlir::TupleType getNodeEntryType(mlir::MLIRContext* ctxt, mlir::Type payload) {
   auto i1Type = mlir::IntegerType::get(ctxt, 1);
   auto i8Type = mlir::IntegerType::get(ctxt, 8);
   auto i32Type = mlir::IntegerType::get(ctxt, 32);
   return mlir::TupleType::get(ctxt, {i32Type, i1Type, i8Type, i8Type, i8Type, i8Type, i8Type, i8Type, payload});
}

inline mlir::TupleType getEdgeEntryType(mlir::MLIRContext* ctxt, mlir::Type payload) {
   auto i1Type = mlir::IntegerType::get(ctxt, 1);
   auto i8Type = mlir::IntegerType::get(ctxt, 8);
   auto i32Type = mlir::IntegerType::get(ctxt, 32);
   return mlir::TupleType::get(ctxt, {i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i1Type, i8Type, payload});
}

} // namespace gengodb::compiler::dialect

#endif // GENGODB_COMPILER_CONVERSION_GRAPHSUBOPTOCF_GRAPHENTRYTYPES_H