#ifndef GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTPHYSICALLAYOUT_H
#define GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTPHYSICALLAYOUT_H

#include "gengodb/semantics/Datatypes.h"

#include "lingodb/compiler/Dialect/util/UtilOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"

namespace gengodb::compiler::dialect::variant::layout {

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
   mlir::Value isBool, isLong, isDouble, isRDFNode, isString, isIri, isUnspecified, isNumericFamily, isScratchPayload, isInteger, isDecimal, isByteString;
};

struct FixedNumericTag {
   gengodb::semantics::xsd::Type type;
   unsigned bitWidth;
   bool isFloat;
   bool isUnsigned;
};

inline llvm::ArrayRef<FixedNumericTag> fixedNumericTags() {
   namespace xsd = gengodb::semantics::xsd;
   static const FixedNumericTag kTags[] = {
      {xsd::Type::Byte, 8, false, false},
      {xsd::Type::Short, 16, false, false},
      {xsd::Type::Int, 32, false, false},
      {xsd::Type::Long, 64, false, false},
      {xsd::Type::UnsignedByte, 8, false, true},
      {xsd::Type::UnsignedShort, 16, false, true},
      {xsd::Type::UnsignedInt, 32, false, true},
      {xsd::Type::UnsignedLong, 64, false, true},
      {xsd::Type::Float, 32, true, false},
      {xsd::Type::Double, 64, true, false},
   };
   return kTags;
}

inline mlir::Type fixedNumericMlirType(mlir::OpBuilder& b, const FixedNumericTag& n) {
   if (n.isFloat) return n.bitWidth == 32 ? mlir::Type(b.getF32Type()) : mlir::Type(b.getF64Type());
   return b.getIntegerType(n.bitWidth);
}

inline TagPredicates computeTagPredicates(mlir::OpBuilder& b, mlir::Location loc, mlir::Value tag) {
   namespace xsd = gengodb::semantics::xsd;
   TagPredicates p;
   p.isBool = getEqTag(b, loc, tag, xsd::Type::Boolean);
   p.isLong = getEqTag(b, loc, tag, xsd::Type::Long);
   p.isDouble = getEqTag(b, loc, tag, xsd::Type::Double);
   p.isRDFNode = getEqTag(b, loc, tag, xsd::Type::RDFNode);
   p.isString = getEqTag(b, loc, tag, xsd::Type::String);
   p.isIri = getEqTag(b, loc, tag, xsd::Type::AnyIRI);
   p.isUnspecified = getEqTag(b, loc, tag, xsd::Type::Unspecified);
   p.isInteger = getEqTag(b, loc, tag, xsd::Type::Integer);
   p.isDecimal = getEqTag(b, loc, tag, xsd::Type::Decimal);
   mlir::Value isByte = getEqTag(b, loc, tag, xsd::Type::Byte);
   mlir::Value isShort = getEqTag(b, loc, tag, xsd::Type::Short);
   mlir::Value isInt = getEqTag(b, loc, tag, xsd::Type::Int);
   mlir::Value isUByte = getEqTag(b, loc, tag, xsd::Type::UnsignedByte);
   mlir::Value isUShort = getEqTag(b, loc, tag, xsd::Type::UnsignedShort);
   mlir::Value isUInt = getEqTag(b, loc, tag, xsd::Type::UnsignedInt);
   mlir::Value isULong = getEqTag(b, loc, tag, xsd::Type::UnsignedLong);
   mlir::Value isFloat = getEqTag(b, loc, tag, xsd::Type::Float);
   mlir::Value isDate = getEqTag(b, loc, tag, xsd::Type::Date);
   mlir::Value isDateTime = getEqTag(b, loc, tag, xsd::Type::DateTime);
   p.isNumericFamily = orAll(b, loc, {p.isBool, p.isLong, p.isDouble, isByte, isShort, isInt, isUByte, isUShort, isUInt, isULong, isFloat, isDate, isDateTime});
   p.isScratchPayload = orAll(b, loc, {p.isNumericFamily, p.isRDFNode, p.isString, p.isIri});
   p.isByteString = orAll(b, loc, {p.isString, p.isInteger, p.isDecimal});
   return p;
}

} // namespace gengodb::compiler::dialect::variant::layout

#endif // GENGODB_COMPILER_CONVERSION_VARIANTTOSTD_VARIANTPHYSICALLAYOUT_H
