#ifndef GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTPHYSICALLAYOUT_H
#define GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTPHYSICALLAYOUT_H

#include "gengodb/semantics/Datatypes.h"

#include "lingodb/compiler/Dialect/util/UtilOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"

namespace gengodb::compiler::variant::layout {

inline mlir::Type getPointerType(mlir::MLIRContext* ctxt) {
   return lingodb::compiler::dialect::util::RefType::get(ctxt, mlir::IntegerType::get(ctxt, 8));
}

inline mlir::TupleType getVariantTupleType(mlir::MLIRContext* ctxt) {
   return mlir::TupleType::get(ctxt, {mlir::IntegerType::get(ctxt, 32), getPointerType(ctxt)});
}

inline mlir::Value constI32(mlir::OpBuilder& b, mlir::Location loc, int32_t v) {
   return b.create<mlir::arith::ConstantIntOp>(loc, v, 32);
}

inline mlir::Value getEqTag(mlir::OpBuilder& b, mlir::Location loc, mlir::Value tag, gengodb::semantics::xsd::Type t) {
   return b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, tag, constI32(b, loc, gengodb::semantics::xsd::to_int32(t)));
}

inline std::pair<mlir::Value, mlir::Value> unpackVariant(mlir::OpBuilder& b, mlir::Location loc, mlir::Value variant) {
   auto unpacked = b.create<lingodb::compiler::dialect::util::UnPackOp>(loc, variant);
   return {unpacked.getResult(0), unpacked.getResult(1)};
}

inline mlir::Value loadTyped(mlir::OpBuilder& b, mlir::Location loc, mlir::Value opaqueRef, mlir::Type type) {
   using namespace lingodb::compiler::dialect;
   mlir::Value typedRef = b.create<util::GenericMemrefCastOp>(loc, util::RefType::get(b.getContext(), type), opaqueRef);
   return b.create<util::LoadOp>(loc, type, typedRef, mlir::Value());
}

inline mlir::Value orAll(mlir::OpBuilder& b, mlir::Location loc, llvm::ArrayRef<mlir::Value> vals) {
   mlir::Value acc = vals.front();
   for (mlir::Value v : vals.drop_front()) acc = b.create<mlir::arith::OrIOp>(loc, acc, v);
   return acc;
}

struct TagPredicates {
   mlir::Value isBool, isLong, isDouble, isRDFNode, isString, isUnspecified, isNumericFamily, isScratchPayload;
};

inline TagPredicates computeTagPredicates(mlir::OpBuilder& b, mlir::Location loc, mlir::Value tag) {
   namespace xsd = gengodb::semantics::xsd;
   TagPredicates p;
   p.isBool = getEqTag(b, loc, tag, xsd::Type::Boolean);
   p.isLong = getEqTag(b, loc, tag, xsd::Type::Long);
   p.isDouble = getEqTag(b, loc, tag, xsd::Type::Double);
   p.isRDFNode = getEqTag(b, loc, tag, xsd::Type::RDFNode);
   p.isString = getEqTag(b, loc, tag, xsd::Type::String);
   p.isUnspecified = getEqTag(b, loc, tag, xsd::Type::Unspecified);
   mlir::Value isByte = getEqTag(b, loc, tag, xsd::Type::Byte);
   mlir::Value isShort = getEqTag(b, loc, tag, xsd::Type::Short);
   mlir::Value isInt = getEqTag(b, loc, tag, xsd::Type::Int);
   mlir::Value isUByte = getEqTag(b, loc, tag, xsd::Type::UnsignedByte);
   mlir::Value isUShort = getEqTag(b, loc, tag, xsd::Type::UnsignedShort);
   mlir::Value isUInt = getEqTag(b, loc, tag, xsd::Type::UnsignedInt);
   mlir::Value isULong = getEqTag(b, loc, tag, xsd::Type::UnsignedLong);
   mlir::Value isFloat = getEqTag(b, loc, tag, xsd::Type::Float);
   mlir::Value isDate = getEqTag(b, loc, tag, xsd::Type::Date);
   p.isNumericFamily = orAll(b, loc, {p.isBool, p.isLong, p.isDouble, isByte, isShort, isInt, isUByte, isUShort, isUInt, isULong, isFloat, isDate});
   p.isScratchPayload = orAll(b, loc, {p.isNumericFamily, p.isRDFNode, p.isString});
   return p;
}

} // namespace gengodb::compiler::variant::layout

#endif // GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTPHYSICALLAYOUT_H
