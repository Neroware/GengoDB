static mlir::TupleType getNodeEntryType(gsubop::NodeRefType t, mlir::TypeConverter& converter) {
   auto i1Type = IntegerType::get(t.getContext(), 1);
   auto i8Type = IntegerType::get(t.getContext(), 8);
   auto i32Type = IntegerType::get(t.getContext(), 32);
   auto propertyTupleType = EntryStorageHelper(nullptr, t.getPropertyMembers(), false, &converter).getStorageType();
   return mlir::TupleType::get(t.getContext(), {i32Type, i1Type, i8Type, i8Type, i8Type, i8Type, i8Type, i8Type, propertyTupleType});
}
static mlir::TupleType getEdgeEntryType(gsubop::EdgeRefType t, mlir::TypeConverter& converter) {
   auto i1Type = IntegerType::get(t.getContext(), 1);
   auto i8Type = IntegerType::get(t.getContext(), 8);
   auto i32Type = IntegerType::get(t.getContext(), 32);
   auto propertyTupleType = EntryStorageHelper(nullptr, t.getPropertyMembers(), false, &converter).getStorageType();
   return mlir::TupleType::get(t.getContext(), {i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i1Type, i8Type, propertyTupleType});
}
static mlir::TupleType getPropertyType(gsubop::TypedPropertyRefType t, mlir::TypeConverter& converter) {
   auto valTupleType = EntryStorageHelper(nullptr, t.getMembers(), false, &converter).getStorageType();
   return mlir::cast<mlir::TupleType>(converter.convertType(valTupleType));
}
static mlir::TupleType getPropertyEntryType(gsubop::PropertyRefType t, mlir::TypeConverter& converter) {
   auto i1Type = IntegerType::get(t.getContext(), 1);
   auto i32Type = IntegerType::get(t.getContext(), 32);
   return mlir::TupleType::get(t.getContext(), {i32Type, i32Type, i32Type, i32Type, i32Type, i1Type});
}
static mlir::TupleType getNodeEntryType(mlir::MLIRContext* ctxt, mlir::Type payload) {
   auto i1Type = IntegerType::get(ctxt, 1);
   auto i8Type = IntegerType::get(ctxt, 8);
   auto i32Type = IntegerType::get(ctxt, 32);
   return mlir::TupleType::get(ctxt, {i32Type, i1Type, i8Type, i8Type, i8Type, i8Type, i8Type, i8Type, payload});
}
static mlir::TupleType getEdgeEntryType(mlir::MLIRContext* ctxt, mlir::Type payload) {
   auto i1Type = IntegerType::get(ctxt, 1);
   auto i8Type = IntegerType::get(ctxt, 8);
   auto i32Type = IntegerType::get(ctxt, 32);
   return mlir::TupleType::get(ctxt, {i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i32Type, i1Type, i8Type, payload});
}

