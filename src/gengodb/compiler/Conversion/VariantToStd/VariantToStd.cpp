#include "gengodb/compiler/Conversion/VariantToStd/VariantToStdPass.h"

#include "gengodb/compiler/Conversion/VariantToStd/VariantPhysicalLayout.h"
#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"
#include "gengodb/compiler/Dialect/Variant/VariantOps.h"
#include "gengodb/semantics/Datatypes.h"

//#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "lingodb/compiler/Dialect/Arrow/IR/ArrowDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamDialect.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"
#include "lingodb/compiler/runtime/StringRuntime.h"
#include "lingodb/gengodb/runtime/Variant.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
namespace {
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;
using namespace gengodb::compiler::variant::layout;
namespace rt = lingodb::compiler::runtime;
namespace xsd = gengodb::semantics::xsd;

// Shared helpers

mlir::Type getVarlen32Type(MLIRContext* ctxt) { return util::VarLen32Type::get(ctxt); }
mlir::Value getPointer(OpBuilder& b, Location loc, Value ref) {
    auto opaque = getPointerType(b.getContext());
    if (ref.getType() == opaque) return ref;
    return b.create<mlir::UnrealizedConversionCastOp>(loc, opaque, ref).getResult(0);
}
Value constBool(OpBuilder& b, Location loc, bool v) { return b.create<arith::ConstantIntOp>(loc, v ? 1 : 0, 1); }
Value packVariant(OpBuilder& b, Location loc, Value tag, Value ref) {
    return b.create<util::PackOp>(loc, getVariantTupleType(b.getContext()), mlir::ValueRange{tag, ref});
}
Value asNullable(OpBuilder& b, Location loc, Type nullableResultType, Value val, Value isNull) {
    return b.create<db::AsNullableOp>(loc, nullableResultType, val, isNull);
}
Value nullValue(OpBuilder& b, Location loc, Type nullableResultType) {
    return b.create<db::NullOp>(loc, nullableResultType).getResult();
}
Value packFromTriBool(OpBuilder& b, Location loc, Type nullableResultType, Value raw) {
    Value minusOne = b.create<arith::ConstantOp>(loc, b.getI8Type(), b.getIntegerAttr(b.getI8Type(), -1));
    Value one = b.create<arith::ConstantOp>(loc, b.getI8Type(), b.getIntegerAttr(b.getI8Type(), 1));
    Value isNull = b.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, raw, minusOne);
    Value boolVal = b.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, raw, one);
    return asNullable(b, loc, nullableResultType, boolVal, isNull);
}
Value createAlloca(ConversionPatternRewriter& rewriter, Operation* anchor, Type elementType, std::optional<int64_t> count = std::nullopt) {
    Operation* nearestIsolated = anchor->getParentOp();
    while (nearestIsolated && !nearestIsolated->hasTrait<mlir::OpTrait::IsIsolatedFromAbove>()) {
        nearestIsolated = nearestIsolated->getParentOp();
    }
    auto funcOp = mlir::dyn_cast_or_null<func::FuncOp>(nearestIsolated);
    if (!funcOp) {
        Value dynamicSize = count ? rewriter.create<arith::ConstantIndexOp>(anchor->getLoc(), *count).getResult() : Value();
        return rewriter.create<util::AllocaOp>(anchor->getLoc(), util::RefType::get(rewriter.getContext(), elementType), dynamicSize);
    }
    OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(&funcOp.getBody().front());
    Value dynamicSize = count ? rewriter.create<arith::ConstantIndexOp>(funcOp.getLoc(), *count).getResult() : Value();
    return rewriter.create<util::AllocaOp>(funcOp.getLoc(), util::RefType::get(rewriter.getContext(), elementType), dynamicSize);
}
std::optional<int32_t> tagForScalarType(mlir::Type t) {
    if (t.isInteger(1)) return xsd::to_int32(xsd::Type::Boolean);
    if (t.isSignlessInteger(8)) return xsd::to_int32(xsd::Type::Byte);
    if (t.isSignlessInteger(16)) return xsd::to_int32(xsd::Type::Short);
    if (t.isSignlessInteger(32)) return xsd::to_int32(xsd::Type::Int);
    if (t.isSignlessInteger(64)) return xsd::to_int32(xsd::Type::Long);
    if (t.isF32()) return xsd::to_int32(xsd::Type::Float);
    if (t.isF64()) return xsd::to_int32(xsd::Type::Double);
    if (mlir::isa<db::StringType>(t)) return xsd::to_int32(xsd::Type::String);
    return std::nullopt;
}
arith::CmpIPredicate toCmpIPredicate(variant::VariantCmpPredicate p) {
    switch (p) {
        case variant::VariantCmpPredicate::eq: return arith::CmpIPredicate::eq;
        case variant::VariantCmpPredicate::neq: return arith::CmpIPredicate::ne;
        case variant::VariantCmpPredicate::lt: return arith::CmpIPredicate::slt;
        case variant::VariantCmpPredicate::lte: return arith::CmpIPredicate::sle;
        case variant::VariantCmpPredicate::gt: return arith::CmpIPredicate::sgt;
        case variant::VariantCmpPredicate::gte: return arith::CmpIPredicate::sge;
    }
    llvm_unreachable("unhandled VariantCmpPredicate");
}
arith::CmpFPredicate toCmpFPredicate(variant::VariantCmpPredicate p) {
    switch (p) {
        case variant::VariantCmpPredicate::eq: return arith::CmpFPredicate::OEQ;
        case variant::VariantCmpPredicate::neq: return arith::CmpFPredicate::ONE;
        case variant::VariantCmpPredicate::lt: return arith::CmpFPredicate::OLT;
        case variant::VariantCmpPredicate::lte: return arith::CmpFPredicate::OLE;
        case variant::VariantCmpPredicate::gt: return arith::CmpFPredicate::OGT;
        case variant::VariantCmpPredicate::gte: return arith::CmpFPredicate::OGE;
    }
    llvm_unreachable("unhandled VariantCmpPredicate");
}
Value applyStringCmp(OpBuilder& b, Location loc, variant::VariantCmpPredicate p, Value lhs, Value rhs) {
    switch (p) {
        case variant::VariantCmpPredicate::eq: return rt::StringRuntime::compareEq(b, loc)({lhs, rhs})[0];
        case variant::VariantCmpPredicate::neq: return rt::StringRuntime::compareNEq(b, loc)({lhs, rhs})[0];
        case variant::VariantCmpPredicate::lt: return rt::StringRuntime::compareLt(b, loc)({lhs, rhs})[0];
        case variant::VariantCmpPredicate::lte: return rt::StringRuntime::compareLte(b, loc)({lhs, rhs})[0];
        case variant::VariantCmpPredicate::gt: return rt::StringRuntime::compareGt(b, loc)({lhs, rhs})[0];
        case variant::VariantCmpPredicate::gte: return rt::StringRuntime::compareGte(b, loc)({lhs, rhs})[0];
    }
    llvm_unreachable("unhandled VariantCmpPredicate");
}
Value notB(OpBuilder& b, Location loc, Value v) {
    return b.create<arith::XOrIOp>(loc, v, constBool(b, loc, true));
}

// Lowering patterns

class CreateScalarOpLowering : public OpConversionPattern<variant::CreateScalarOp> {
    public:
    using OpConversionPattern<variant::CreateScalarOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::CreateScalarOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto* ctxt = getContext();
        auto valType = adaptor.getValue().getType();
        auto tagOpt = tagForScalarType(valType);
        if (!tagOpt) return rewriter.notifyMatchFailure(op, "unsupported scalar type for variant.create_scalar (see tagForScalarType)");
        Value typedRef = createAlloca(rewriter, op, valType);
        rewriter.create<util::StoreOp>(loc, adaptor.getValue(), typedRef, Value());
        Value opaqueRef = rewriter.create<util::GenericMemrefCastOp>(loc, getPointerType(ctxt), typedRef);
        Value tag = constI32(rewriter, loc, *tagOpt);
        rewriter.replaceOp(op, packVariant(rewriter, loc, tag, opaqueRef));
        return success();
    }
};

class CreateNodeRefOpLowering : public OpConversionPattern<variant::CreateNodeRefOp> {
    public:
    using OpConversionPattern<variant::CreateNodeRefOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::CreateNodeRefOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto* ctxt = getContext();
        Value opaqueRef = getPointer(rewriter, loc, adaptor.getRef());
        Value tag = rt::VariantRuntime::resolveRefTag(rewriter, loc)({opaqueRef})[0];
        auto tagPred = computeTagPredicates(rewriter, loc, tag);
        auto opaqueType = getPointerType(ctxt);

        auto outerIf = rewriter.create<scf::IfOp>(
            loc, tagPred.isNumericFamily,
            [&](OpBuilder& b, Location l) {
                Value slot = createAlloca(rewriter, op, b.getIntegerType(8), 8);
                rt::VariantRuntime::extractNumericLiteral(b, l)({opaqueRef, tag, slot});
                b.create<scf::YieldOp>(l, slot);
            },
            [&](OpBuilder& b, Location l) {
                auto innerIf = b.create<scf::IfOp>(
                    l, tagPred.isRDFNode,
                    [&](OpBuilder& b2, Location l2) {
                        b2.create<scf::YieldOp>(l2, opaqueRef);
                    },
                    [&](OpBuilder& b2, Location l2) {
                        auto strIf = b2.create<scf::IfOp>(
                            l2, tagPred.isString,
                            [&](OpBuilder& b3, Location l3) {
                                Value strVal = rt::VariantRuntime::extractBlobLiteral(b3, l3)({opaqueRef})[0];
                                Value strSlot = createAlloca(rewriter, op, getVarlen32Type(ctxt));
                                b3.create<util::StoreOp>(l3, strVal, strSlot, Value());
                                Value opaqueStrSlot = b3.create<util::GenericMemrefCastOp>(l3, opaqueType, strSlot);
                                b3.create<scf::YieldOp>(l3, opaqueStrSlot);
                            },
                            [&](OpBuilder& b3, Location l3) {
                                b3.create<scf::YieldOp>(l3, opaqueRef);
                            });
                        b2.create<scf::YieldOp>(l2, strIf.getResult(0));
                    });
                b.create<scf::YieldOp>(l, innerIf.getResult(0));
            });
        rewriter.replaceOp(op, packVariant(rewriter, loc, tag, outerIf.getResult(0)));
        return success();
    }
};

class VariantIsAOpLowering : public OpConversionPattern<variant::VariantIsAOp> {
    public:
    using OpConversionPattern<variant::VariantIsAOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::VariantIsAOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVar());
        Value matches = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, tag, adaptor.getTypeId());
        rewriter.replaceOp(op, matches);
        return success();
    }
};

class VariantGetValOpLowering : public OpConversionPattern<variant::VariantGetValOp> {
    public:
    using OpConversionPattern<variant::VariantGetValOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::VariantGetValOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto resultType = op.getResult().getType();
        // unsafe operation always returns a value, never !db.nullable<...>
        if (mlir::isa<db::NullableType>(resultType)) return rewriter.notifyMatchFailure(op, "variant.variant_get_val is an unsafe direct unwrap and must not target !db.nullable<...> -- check the tag with variant.variant_is_a first if a safe unwrap is needed");
        Value ref = unpackVariant(rewriter, loc, adaptor.getVal()).second;
        if (mlir::isa<util::RefType>(resultType)) {
            rewriter.replaceOp(op, ref);
            return success();
        }
        rewriter.replaceOp(op, loadTyped(rewriter, loc, ref, resultType));
        return success();
    }
};

class ToStringOpLowering : public OpConversionPattern<variant::ToStringOp> {
    public:
    using OpConversionPattern<variant::ToStringOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::ToStringOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVal());
        auto tp = computeTagPredicates(rewriter, loc, tag);

        auto result = rewriter.create<scf::IfOp>(
            loc, tp.isNumericFamily,
            [&](OpBuilder& b, Location l) {
                b.create<scf::YieldOp>(l, rt::VariantRuntime::toStringNumeric(b, l)({ref, tag})[0]);
            },
            [&](OpBuilder& b, Location l) {
                auto ifRDFNode = b.create<scf::IfOp>(
                    l, tp.isRDFNode,
                    [&](OpBuilder& b2, Location l2) {
                        b2.create<scf::YieldOp>(l2, rt::VariantRuntime::toStringNodeRef(b2, l2)({ref})[0]);
                    },
                    [&](OpBuilder& b2, Location l2) {
                        auto ifString = b2.create<scf::IfOp>(
                            l2, tp.isString,
                            [&](OpBuilder& b3, Location l3) {
                            b3.create<scf::YieldOp>(l3, loadTyped(b3, l3, ref, getVarlen32Type(getContext())));
                            },
                            [&](OpBuilder& b3, Location l3) {
                                b3.create<scf::YieldOp>(l3, rt::VariantRuntime::toStringBlobLiteral(b3, l3)({ref})[0]);
                            });
                        b2.create<scf::YieldOp>(l2, ifString.getResult(0));
                    });
                b.create<scf::YieldOp>(l, ifRDFNode.getResult(0));
            });
        Value asDbString = rewriter.create<mlir::UnrealizedConversionCastOp>(loc, op.getResult().getType(), result.getResult(0)).getResult(0);
        rewriter.replaceOp(op, asDbString);
        return success();
   }
};

class CmpOpLowering : public OpConversionPattern<variant::CmpOp> {
    public:
    using OpConversionPattern<variant::CmpOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::CmpOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        if (!mlir::isa<db::NullableType>(op.getResult().getType())) return rewriter.notifyMatchFailure(op, "variant.cmp result must be !db.nullable<i1>");
        auto resultType = op.getResult().getType();
        auto [lhsTag, lhsRef] = unpackVariant(rewriter, loc, adaptor.getLhs());
        auto [rhsTag, rhsRef] = unpackVariant(rewriter, loc, adaptor.getRhs());
        auto lp = computeTagPredicates(rewriter, loc, lhsTag);
        auto rp = computeTagPredicates(rewriter, loc, rhsTag);
        Value sameTag = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, lhsTag, rhsTag);
        Value predConst = constI32(rewriter, loc, static_cast<int32_t>(op.getPredicate()));
        auto predicate = op.getPredicate();

        auto outer = rewriter.create<scf::IfOp>(
            loc, sameTag,
            [&](OpBuilder& b, Location l) {
                auto ifBool = b.create<scf::IfOp>(
                    l, lp.isBool,
                    [&](OpBuilder& b2, Location l2) {
                        Value lv = loadTyped(b2, l2, lhsRef, b2.getI1Type());
                        Value rv = loadTyped(b2, l2, rhsRef, b2.getI1Type());
                        Value cmp = b2.create<arith::CmpIOp>(l2, toCmpIPredicate(predicate), lv, rv);
                        b2.create<scf::YieldOp>(l2, asNullable(b2, l2, resultType, cmp, constBool(b2, l2, false)));
                    },
                    [&](OpBuilder& b2, Location l2) {
                        auto ifI64 = b2.create<scf::IfOp>(
                            l2, lp.isLong,
                            [&](OpBuilder& b3, Location l3) {
                            Value lv = loadTyped(b3, l3, lhsRef, b3.getI64Type());
                            Value rv = loadTyped(b3, l3, rhsRef, b3.getI64Type());
                            Value cmp = b3.create<arith::CmpIOp>(l3, toCmpIPredicate(predicate), lv, rv);
                            b3.create<scf::YieldOp>(l3, asNullable(b3, l3, resultType, cmp, constBool(b3, l3, false)));
                            },
                            [&](OpBuilder& b3, Location l3) {
                            auto ifDouble = b3.create<scf::IfOp>(
                                l3, lp.isDouble,
                                [&](OpBuilder& b4, Location l4) {
                                    Value lv = loadTyped(b4, l4, lhsRef, b4.getF64Type());
                                    Value rv = loadTyped(b4, l4, rhsRef, b4.getF64Type());
                                    Value cmp = b4.create<arith::CmpFOp>(l4, toCmpFPredicate(predicate), lv, rv);
                                    b4.create<scf::YieldOp>(l4, asNullable(b4, l4, resultType, cmp, constBool(b4, l4, false)));
                                },
                                [&](OpBuilder& b4, Location l4) {
                                    auto ifRemainingNumeric = b4.create<scf::IfOp>(
                                        l4, lp.isNumericFamily,
                                        [&](OpBuilder& b5, Location l5) {
                                            Value raw = rt::VariantRuntime::compareNumericCross(b5, l5)({lhsRef, lhsTag, rhsRef, rhsTag, predConst})[0];
                                            b5.create<scf::YieldOp>(l5, packFromTriBool(b5, l5, resultType, raw));
                                        },
                                        [&](OpBuilder& b5, Location l5) {
                                            auto ifString = b5.create<scf::IfOp>(
                                                l5, lp.isString,
                                                [&](OpBuilder& b6, Location l6) {
                                                    Value lv = loadTyped(b6, l6, lhsRef, getVarlen32Type(getContext()));
                                                    Value rv = loadTyped(b6, l6, rhsRef, getVarlen32Type(getContext()));
                                                    Value cmp = applyStringCmp(b6, l6, predicate, lv, rv);
                                                    b6.create<scf::YieldOp>(l6, asNullable(b6, l6, resultType, cmp, constBool(b6, l6, false)));
                                                },
                                                [&](OpBuilder& b6, Location l6) {
                                                    auto ifRDFNode = b6.create<scf::IfOp>(
                                                        l6, lp.isRDFNode,
                                                        [&](OpBuilder& b7, Location l7) {
                                                        Value raw = rt::VariantRuntime::compareNodeRefRef(b7, l7)({lhsRef, rhsRef, predConst})[0];
                                                        b7.create<scf::YieldOp>(l7, packFromTriBool(b7, l7, resultType, raw));
                                                        },
                                                        [&](OpBuilder& b7, Location l7) {
                                                        auto ifUnspecified = b7.create<scf::IfOp>(
                                                            l7, lp.isUnspecified,
                                                            [&](OpBuilder& b8, Location l8) {
                                                                b8.create<scf::YieldOp>(l8, nullValue(b8, l8, resultType));
                                                            },
                                                            [&](OpBuilder& b8, Location l8) {
                                                                Value raw = rt::VariantRuntime::compareBlobLiteralRefRef(b8, l8)({lhsRef, rhsRef, predConst})[0];
                                                                b8.create<scf::YieldOp>(l8, packFromTriBool(b8, l8, resultType, raw));
                                                            });
                                                        b7.create<scf::YieldOp>(l7, ifUnspecified.getResult(0));
                                                        });
                                                    b6.create<scf::YieldOp>(l6, ifRDFNode.getResult(0));
                                                });
                                            b5.create<scf::YieldOp>(l5, ifString.getResult(0));
                                        });
                                    b4.create<scf::YieldOp>(l4, ifRemainingNumeric.getResult(0));
                                });
                            b3.create<scf::YieldOp>(l3, ifDouble.getResult(0));
                            });
                        b2.create<scf::YieldOp>(l2, ifI64.getResult(0));
                    });
                b.create<scf::YieldOp>(l, ifBool.getResult(0));
            },
            [&](OpBuilder& b, Location l) {
                Value bothNumeric = b.create<arith::AndIOp>(l, lp.isNumericFamily, rp.isNumericFamily);
                auto ifNumeric = b.create<scf::IfOp>(
                    l, bothNumeric,
                    [&](OpBuilder& b2, Location l2) {
                        Value raw = rt::VariantRuntime::compareNumericCross(b2, l2)({lhsRef, lhsTag, rhsRef, rhsTag, predConst})[0];
                        b2.create<scf::YieldOp>(l2, packFromTriBool(b2, l2, resultType, raw));
                    },
                    [&](OpBuilder& b2, Location l2) {
                        Value neitherScratch = b2.create<arith::AndIOp>(l2, notB(b2, l2, lp.isScratchPayload), notB(b2, l2, rp.isScratchPayload));
                        auto ifLiteral = b2.create<scf::IfOp>(
                            l2, neitherScratch,
                            [&](OpBuilder& b3, Location l3) {
                                Value raw = rt::VariantRuntime::compareBlobLiteralRefRef(b3, l3)({lhsRef, rhsRef, predConst})[0];
                                b3.create<scf::YieldOp>(l3, packFromTriBool(b3, l3, resultType, raw));
                            },
                            [&](OpBuilder& b3, Location l3) {
                                b3.create<scf::YieldOp>(l3, nullValue(b3, l3, resultType));
                            });
                        b2.create<scf::YieldOp>(l2, ifLiteral.getResult(0));
                    });
                b.create<scf::YieldOp>(l, ifNumeric.getResult(0));
            });
        rewriter.replaceOp(op, outer.getResult(0));
        return success();
    }
};

Value applyIntArith(OpBuilder& b, Location loc, variant::VariantArithPredicate p, Value lhs, Value rhs) {
    switch (p) {
        case variant::VariantArithPredicate::add: return b.create<arith::AddIOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::sub: return b.create<arith::SubIOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::mul: return b.create<arith::MulIOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::div: return b.create<arith::DivSIOp>(loc, lhs, rhs);
    }
    llvm_unreachable("unhandled VariantArithPredicate");
}
Value applyFloatArith(OpBuilder& b, Location loc, variant::VariantArithPredicate p, Value lhs, Value rhs) {
    switch (p) {
        case variant::VariantArithPredicate::add: return b.create<arith::AddFOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::sub: return b.create<arith::SubFOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::mul: return b.create<arith::MulFOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::div: return b.create<arith::DivFOp>(loc, lhs, rhs);
    }
    llvm_unreachable("unhandled VariantArithPredicate");
}

class ArithOpLowering : public OpConversionPattern<variant::ArithOp> {
    public:
    using OpConversionPattern<variant::ArithOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::ArithOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto* ctxt = getContext();
        auto [lhsTag, lhsRef] = unpackVariant(rewriter, loc, adaptor.getLhs());
        auto [rhsTag, rhsRef] = unpackVariant(rewriter, loc, adaptor.getRhs());
        Value sameTag = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, lhsTag, rhsTag);
        Value longTag = constI32(rewriter, loc, xsd::to_int32(xsd::Type::Long));
        Value doubleTag = constI32(rewriter, loc, xsd::to_int32(xsd::Type::Double));
        Value isLong = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, lhsTag, longTag);
        Value isDouble = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, lhsTag, doubleTag);
        bool longFastEligible = op.getPredicate() != variant::VariantArithPredicate::div;
        Value fastLong = longFastEligible ? isLong : constBool(rewriter, loc, false);
        Value fast = rewriter.create<arith::AndIOp>(loc, sameTag, rewriter.create<arith::OrIOp>(loc, fastLong, isDouble));

        auto predicate = op.getPredicate();
        auto result = rewriter.create<scf::IfOp>(
            loc, fast,
            [&](OpBuilder& b, Location l) {
                auto ifLong = b.create<scf::IfOp>(
                    l, isLong,
                    [&](OpBuilder& b2, Location l2) {
                        Value lv = loadTyped(b2, l2, lhsRef, b2.getI64Type());
                        Value rv = loadTyped(b2, l2, rhsRef, b2.getI64Type());
                        Value res = applyIntArith(b2, l2, predicate, lv, rv);
                        Value slot = createAlloca(rewriter, op, b2.getI64Type());
                        b2.create<util::StoreOp>(l2, res, slot, Value());
                        Value opaque = b2.create<util::GenericMemrefCastOp>(l2, getPointerType(ctxt), slot);
                        b2.create<scf::YieldOp>(l2, packVariant(b2, l2, longTag, opaque));
                    },
                    [&](OpBuilder& b2, Location l2) {
                        Value lv = loadTyped(b2, l2, lhsRef, b2.getF64Type());
                        Value rv = loadTyped(b2, l2, rhsRef, b2.getF64Type());
                        Value res = applyFloatArith(b2, l2, predicate, lv, rv);
                        Value slot = createAlloca(rewriter, op, b2.getF64Type());
                        b2.create<util::StoreOp>(l2, res, slot, Value());
                        Value opaque = b2.create<util::GenericMemrefCastOp>(l2, getPointerType(ctxt), slot);
                        b2.create<scf::YieldOp>(l2, packVariant(b2, l2, doubleTag, opaque));
                    });
                b.create<scf::YieldOp>(l, ifLong.getResult(0));
            },
            [&](OpBuilder& b, Location l) {
                Value predConst = constI32(b, l, static_cast<int32_t>(predicate));
                Value outSlot = createAlloca(rewriter, op, b.getIntegerType(8), 8);
                Value resultTag = rt::VariantRuntime::arithNumericCross(b, l)({lhsRef, lhsTag, rhsRef, rhsTag, predConst, outSlot})[0];
                b.create<scf::YieldOp>(l, packVariant(b, l, resultTag, outSlot));
            });
        rewriter.replaceOp(op, result.getResult(0));
        return success();
    }
};

struct VariantToStdLoweringPass
    : public mlir::PassWrapper<VariantToStdLoweringPass, OperationPass<mlir::ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(VariantToStdLoweringPass)
    llvm::StringRef getArgument() const override { return "lower-variant-to-std"; }

    void getDependentDialects(DialectRegistry& registry) const override {
        registry.insert<db::DBDialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithDialect /*, gsubop::GraphSubOpDialect */ >();
    }
    void runOnOperation() override {
        auto module = getOperation();
        getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);

        ConversionTarget target(getContext());
        target.addLegalOp<ModuleOp>();
        target.addLegalOp<UnrealizedConversionCastOp>();
        target.addIllegalDialect<variant::VariantDialect>();
        target.addLegalDialect<db::DBDialect>();
        target.addLegalDialect<func::FuncDialect>();
        target.addLegalDialect<memref::MemRefDialect>();
        target.addLegalDialect<arith::ArithDialect>();
        target.addLegalDialect<cf::ControlFlowDialect>();
        target.addLegalDialect<scf::SCFDialect>();
        target.addLegalDialect<util::UtilDialect>();
        target.addLegalDialect<arrow::ArrowDialect>();
        //target.addLegalDialect<gsubop::GraphSubOpDialect>();
        target.addLegalDialect<subop::SubOperatorDialect>();
        target.addLegalDialect<tuples::TupleStreamDialect>();

        auto* ctxt = &getContext();
        TypeConverter typeConverter;
        typeConverter.addConversion([&](mlir::Type type) { return type; });
        typeConverter.addConversion([&](variant::VariantType type) -> mlir::Type {
            return getVariantTupleType(type.getContext());
        });
        typeConverter.addSourceMaterialization([](OpBuilder& builder, mlir::Type resultType, mlir::ValueRange inputs, mlir::Location loc) -> mlir::Value {
            if (inputs.size() != 1) return nullptr;
            return builder.create<mlir::UnrealizedConversionCastOp>(loc, resultType, inputs).getResult(0);
        });

        RewritePatternSet patterns(ctxt);
        patterns.insert<CreateScalarOpLowering>(typeConverter, ctxt);
        patterns.insert<CreateNodeRefOpLowering>(typeConverter, ctxt);
        patterns.insert<VariantIsAOpLowering>(typeConverter, ctxt);
        patterns.insert<VariantGetValOpLowering>(typeConverter, ctxt);
        patterns.insert<ToStringOpLowering>(typeConverter, ctxt);
        patterns.insert<CmpOpLowering>(typeConverter, ctxt);
        patterns.insert<ArithOpLowering>(typeConverter, ctxt);

        if (failed(applyFullConversion(module, target, std::move(patterns)))) {
            signalPassFailure();
        }
    }
};
} // namespace

std::unique_ptr<mlir::Pass> variant::createLowerVariantToStdPass() {
   return std::make_unique<VariantToStdLoweringPass>();
}
void variant::registerVariantToStdConversionPasses() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return variant::createLowerVariantToStdPass();
   });
}
