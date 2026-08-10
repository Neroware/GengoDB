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

#include <functional>

using namespace mlir;
namespace {
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;
using namespace gengodb::compiler::dialect::variant::layout;
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
Value allocStack(ConversionPatternRewriter& rewriter, Operation* anchor, Type elementType, std::optional<int64_t> count = std::nullopt) {
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
Value allocScratch(OpBuilder& b, Location loc, mlir::Type elementType, int64_t count = 1) {
    Value bytes = b.create<util::SizeOfOp>(loc, b.getIndexType(), elementType);
    if (count != 1) {
        Value countVal = b.create<arith::ConstantIndexOp>(loc, count);
        bytes = b.create<arith::MulIOp>(loc, bytes, countVal);
    }
    Value bytesI64 = b.create<arith::IndexCastOp>(loc, b.getI64Type(), bytes);
    return rt::VariantRuntime::allocScratch(b, loc)({bytesI64})[0];
}

bool isInlineScalarType(mlir::Type t) {
    return t.isInteger(1) || t.isSignlessInteger(8) || t.isSignlessInteger(16) ||
        t.isSignlessInteger(32) || t.isSignlessInteger(64) || t.isF32() || t.isF64();
}
Value widenToI64(OpBuilder& b, Location loc, Value v) {
    auto t = v.getType();
    if (t.isF32()) {
        Value bits32 = b.create<arith::BitcastOp>(loc, b.getI32Type(), v);
        return b.create<arith::ExtUIOp>(loc, b.getI64Type(), bits32);
    }
    if (t.isF64()) return b.create<arith::BitcastOp>(loc, b.getI64Type(), v);
    if (t.isInteger(64)) return v;
    return b.create<arith::ExtUIOp>(loc, b.getI64Type(), v);
}
Value narrowFromI64(OpBuilder& b, Location loc, Value bits, Type target) {
    if (target.isF32()) {
        Value bits32 = b.create<arith::TruncIOp>(loc, b.getI32Type(), bits);
        return b.create<arith::BitcastOp>(loc, b.getF32Type(), bits32);
    }
    if (target.isF64()) return b.create<arith::BitcastOp>(loc, b.getF64Type(), bits);
    if (target.isInteger(64)) return bits;
    return b.create<arith::TruncIOp>(loc, target, bits);
}
Value packInline(OpBuilder& b, Location loc, Value v) {
    return b.create<util::IntToPtrOp>(loc, getPointerType(b.getContext()), widenToI64(b, loc, v));
}
Value unpackInline(OpBuilder& b, Location loc, Value ref, Type target) {
    Value bits = b.create<util::PtrToIntOp>(loc, b.getI64Type(), ref);
    return narrowFromI64(b, loc, bits, target);
}
Value inlineFromScratch(OpBuilder& b, Location loc, Value scratch) {
    Value bits = loadTyped(b, loc, scratch, b.getI64Type());
    return b.create<util::IntToPtrOp>(loc, getPointerType(b.getContext()), bits);
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
arith::CmpIPredicate toCmpIPredicateUnsigned(variant::VariantCmpPredicate p) {
    switch (p) {
        case variant::VariantCmpPredicate::eq: return arith::CmpIPredicate::eq;
        case variant::VariantCmpPredicate::neq: return arith::CmpIPredicate::ne;
        case variant::VariantCmpPredicate::lt: return arith::CmpIPredicate::ult;
        case variant::VariantCmpPredicate::lte: return arith::CmpIPredicate::ule;
        case variant::VariantCmpPredicate::gt: return arith::CmpIPredicate::ugt;
        case variant::VariantCmpPredicate::gte: return arith::CmpIPredicate::uge;
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

using TagCaseBuilder = std::function<Value(OpBuilder&, Location)>;
struct TagCase {
    Value cond;
    TagCaseBuilder build;
};
Value dispatchOnTag(OpBuilder& b, Location loc, llvm::ArrayRef<TagCase> cases, const TagCaseBuilder& fallback) {
    if (cases.empty()) return fallback(b, loc);
    TagCase head = cases.front();
    llvm::ArrayRef<TagCase> rest = cases.drop_front();
    auto ifOp = b.create<scf::IfOp>(
        loc, head.cond,
        [&](OpBuilder& b2, Location l2) { b2.create<scf::YieldOp>(l2, head.build(b2, l2)); },
        [&](OpBuilder& b2, Location l2) { b2.create<scf::YieldOp>(l2, dispatchOnTag(b2, l2, rest, fallback)); });
    return ifOp.getResult(0);
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
        Value tag = constI32(rewriter, loc, *tagOpt);
        if (isInlineScalarType(valType)) {
            rewriter.replaceOp(op, packVariant(rewriter, loc, tag, packInline(rewriter, loc, adaptor.getValue())));
            return success();
        }
        Value strSlot = allocScratch(rewriter, loc, valType);
        Value typedSlot = rewriter.create<util::GenericMemrefCastOp>(loc, util::RefType::get(ctxt, valType), strSlot);
        rewriter.create<util::StoreOp>(loc, adaptor.getValue(), typedSlot, Value());
        rewriter.replaceOp(op, packVariant(rewriter, loc, tag, strSlot));
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

        auto outerIf = rewriter.create<scf::IfOp>(
            loc, tagPred.isNumericFamily,
            [&](OpBuilder& b, Location l) {
                Value scratch = allocStack(rewriter, op, b.getIntegerType(8), 8);
                rt::VariantRuntime::extractNumericLiteral(b, l)({opaqueRef, tag, scratch});
                b.create<scf::YieldOp>(l, inlineFromScratch(b, l, scratch));
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
                                Value strSlot = allocScratch(b3, l3, getVarlen32Type(ctxt));
                                Value typedStrSlot = b3.create<util::GenericMemrefCastOp>(l3, util::RefType::get(ctxt, getVarlen32Type(ctxt)), strSlot);
                                b3.create<util::StoreOp>(l3, strVal, typedStrSlot, Value());
                                b3.create<scf::YieldOp>(l3, strSlot);
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

class UnspecifiedIfOpLowering : public OpConversionPattern<variant::UnspecifiedIfOp> {
    public:
    using OpConversionPattern<variant::UnspecifiedIfOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::UnspecifiedIfOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto* ctxt = getContext();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVal());
        Value cond = adaptor.getCond();
        Value unspecifiedTag = constI32(rewriter, loc, xsd::to_int32(xsd::Type::Unspecified));
        Value noPayload = rewriter.create<util::InvalidRefOp>(loc, getPointerType(ctxt));
        Value resTag = rewriter.create<arith::SelectOp>(loc, cond, unspecifiedTag, tag);
        Value resRef = rewriter.create<arith::SelectOp>(loc, cond, noPayload, ref);
        rewriter.replaceOp(op, packVariant(rewriter, loc, resTag, resRef));
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
        if (isInlineScalarType(resultType)) {
            rewriter.replaceOp(op, unpackInline(rewriter, loc, ref, resultType));
            return success();
        }
        rewriter.replaceOp(op, loadTyped(rewriter, loc, ref, resultType));
        return success();
    }
};

Value computeLexicalForm(OpBuilder& b, Location loc, MLIRContext* ctxt, Value tag, Value ref) {
    auto tp = computeTagPredicates(b, loc, tag);
    auto guarded = b.create<scf::IfOp>(
        loc, tp.isUnspecified,
        [&](OpBuilder& b1, Location l1) {
            Value empty = b1.create<util::CreateConstVarLen>(l1, getVarlen32Type(ctxt), b1.getStringAttr(""));
            b1.create<scf::YieldOp>(l1, empty);
        },
        [&](OpBuilder& b1, Location l1) {
            auto result = b1.create<scf::IfOp>(
                l1, tp.isNumericFamily,
                [&](OpBuilder& b2, Location l2) {
                    Value payload = b2.create<util::PtrToIntOp>(l2, b2.getI64Type(), ref);
                    b2.create<scf::YieldOp>(l2, rt::VariantRuntime::toStringNumeric(b2, l2)({payload, tag})[0]);
                },
                [&](OpBuilder& b2, Location l2) {
                    auto ifRDFNode = b2.create<scf::IfOp>(
                        l2, tp.isRDFNode,
                        [&](OpBuilder& b3, Location l3) {
                            b3.create<scf::YieldOp>(l3, rt::VariantRuntime::toStringNodeRef(b3, l3)({ref})[0]);
                        },
                        [&](OpBuilder& b3, Location l3) {
                            auto ifString = b3.create<scf::IfOp>(
                                l3, tp.isString,
                                [&](OpBuilder& b4, Location l4) {
                                    b4.create<scf::YieldOp>(l4, loadTyped(b4, l4, ref, getVarlen32Type(ctxt)));
                                },
                                [&](OpBuilder& b4, Location l4) {
                                    b4.create<scf::YieldOp>(l4, rt::VariantRuntime::toStringBlobLiteral(b4, l4)({ref})[0]);
                                });
                            b3.create<scf::YieldOp>(l3, ifString.getResult(0));
                        });
                    b2.create<scf::YieldOp>(l2, ifRDFNode.getResult(0));
                });
            b1.create<scf::YieldOp>(l1, result.getResult(0));
        });
    return guarded.getResult(0);
}

class ToStringOpLowering : public OpConversionPattern<variant::ToStringOp> {
    public:
    using OpConversionPattern<variant::ToStringOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::ToStringOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVal());
        Value lex = computeLexicalForm(rewriter, loc, getContext(), tag, ref);
        Value asDbString = rewriter.create<mlir::UnrealizedConversionCastOp>(loc, op.getResult().getType(), lex).getResult(0);
        rewriter.replaceOp(op, asDbString);
        return success();
   }
};

class CastOpLowering : public OpConversionPattern<variant::CastOp> {
    public:
    using OpConversionPattern<variant::CastOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::CastOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        llvm::APInt constVal;
        if (!mlir::matchPattern(adaptor.getTypeId(), mlir::m_ConstantInt(&constVal)))
            return rewriter.notifyMatchFailure(op, "variant.cast requires a compile-time-constant typeId (target type must be statically known)");
        auto targetOpt = xsd::from_int32(static_cast<int32_t>(constVal.getSExtValue()));
        if (!targetOpt || !isSupportedCastTarget(*targetOpt))
            return rewriter.notifyMatchFailure(op, "variant.cast: unsupported target type (only boolean/fixed-width-numeric targets are supported at this layer -- fold xsd:integer/xsd:decimal in the frontend first)");
        xsd::Type target = *targetOpt;

        Value targetTagConst = constI32(rewriter, loc, xsd::to_int32(target));
        auto [srcTag, srcRef] = unpackVariant(rewriter, loc, adaptor.getVar());
        Value sameTag = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, srcTag, targetTagConst);

        auto result = rewriter.create<scf::IfOp>(
            loc, sameTag,
            [&](OpBuilder& b, Location l) {
                b.create<scf::YieldOp>(l, adaptor.getVar());
            },
            [&](OpBuilder& b, Location l) {
                Value payload = b.create<util::PtrToIntOp>(l, b.getI64Type(), srcRef);
                Value outSlot = allocStack(rewriter, op, b.getIntegerType(8), 8);
                Value resultTag = rt::VariantRuntime::castLiteral(b, l)({payload, srcTag, srcRef, targetTagConst, outSlot})[0];
                b.create<scf::YieldOp>(l, packVariant(b, l, resultTag, inlineFromScratch(b, l, outSlot)));
            });
        rewriter.replaceOp(op, result.getResult(0));
        return success();
    }
    private:
    inline static bool isSupportedCastTarget(xsd::Type t) {
        if (t == xsd::Type::Boolean) return true;
        for (const FixedNumericTag& n : fixedNumericTags())
            if (n.type == t) return true;
        return false;
    }
};

class StrCastOpLowering : public OpConversionPattern<variant::StrCastOp> {
    public:
    using OpConversionPattern<variant::StrCastOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::StrCastOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto* ctxt = getContext();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVar());
        Value lex = computeLexicalForm(rewriter, loc, ctxt, tag, ref);
        Value scratch = allocScratch(rewriter, loc, getVarlen32Type(ctxt));
        Value typedScratch = rewriter.create<util::GenericMemrefCastOp>(loc, util::RefType::get(ctxt, getVarlen32Type(ctxt)), scratch);
        rewriter.create<util::StoreOp>(loc, lex, typedScratch, Value());
        Value strTag = constI32(rewriter, loc, xsd::to_int32(xsd::Type::String));
        rewriter.replaceOp(op, packVariant(rewriter, loc, strTag, scratch));
        return success();
    }
};

class LangCastOpLowering : public OpConversionPattern<variant::StrCastOp> {
    public:
    using OpConversionPattern<variant::StrCastOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::StrCastOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        if (!op->hasAttr("isLangCast")) return rewriter.notifyMatchFailure(op, "not a LANG() cast -- defer to the plain str_cast lowering");
        auto loc = op->getLoc();
        auto* ctxt = getContext();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVar());
        Value payload = rewriter.create<util::PtrToIntOp>(loc, rewriter.getI64Type(), ref);
        Value lang = rt::VariantRuntime::langTag(rewriter, loc)({payload, tag, ref})[0];
        Value scratch = allocScratch(rewriter, loc, getVarlen32Type(ctxt));
        Value typedScratch = rewriter.create<util::GenericMemrefCastOp>(loc, util::RefType::get(ctxt, getVarlen32Type(ctxt)), scratch);
        rewriter.create<util::StoreOp>(loc, lang, typedScratch, Value());
        Value strTag = constI32(rewriter, loc, xsd::to_int32(xsd::Type::String));
        rewriter.replaceOp(op, packVariant(rewriter, loc, strTag, scratch));
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

        // Slow runtime-call fallback via rdf4cpp API
        auto numericCrossCmp = [&](OpBuilder& b, Location l) -> Value {
            Value lhsPayload = b.create<util::PtrToIntOp>(l, b.getI64Type(), lhsRef);
            Value rhsPayload = b.create<util::PtrToIntOp>(l, b.getI64Type(), rhsRef);
            Value raw = rt::VariantRuntime::compareNumericCross(b, l)({lhsPayload, lhsTag, rhsPayload, rhsTag, predConst})[0];
            return packFromTriBool(b, l, resultType, raw);
        };
        auto blobLiteralCmp = [&](OpBuilder& b, Location l) -> Value {
            Value raw = rt::VariantRuntime::compareBlobLiteralRefRef(b, l)({lhsRef, rhsRef, predConst})[0];
            return packFromTriBool(b, l, resultType, raw);
        };

        // Same-tag fast paths for fixed scalars up to 8 bytes.
        std::vector<TagCase> fastCases;
        fastCases.push_back({lp.isBool, [&](OpBuilder& b, Location l) -> Value {
            Value lv = unpackInline(b, l, lhsRef, b.getI1Type());
            Value rv = unpackInline(b, l, rhsRef, b.getI1Type());
            Value cmp = b.create<arith::CmpIOp>(l, toCmpIPredicate(predicate), lv, rv);
                return asNullable(b, l, resultType, cmp, constBool(b, l, false));
        }});
        for (const FixedNumericTag& n : fixedNumericTags()) {
            Value isThisTag = getEqTag(rewriter, loc, lhsTag, n.type);
            fastCases.push_back({isThisTag, [&, n](OpBuilder& b, Location l) -> Value {
                mlir::Type ty = fixedNumericMlirType(b, n);
                Value lv = unpackInline(b, l, lhsRef, ty);
                Value rv = unpackInline(b, l, rhsRef, ty);
                Value cmp = n.isFloat
                    ? b.create<arith::CmpFOp>(l, toCmpFPredicate(predicate), lv, rv).getResult()
                    : b.create<arith::CmpIOp>(l, n.isUnsigned ? toCmpIPredicateUnsigned(predicate) : toCmpIPredicate(predicate), lv, rv).getResult();
                        return asNullable(b, l, resultType, cmp, constBool(b, l, false));
            }});
        }
        auto sameTagSlow = [&](OpBuilder& b, Location l) -> Value {
            std::vector<TagCase> cases = {
                {lp.isNumericFamily, numericCrossCmp},
                {lp.isString, [&](OpBuilder& b2, Location l2) -> Value {
                    Value lv = loadTyped(b2, l2, lhsRef, getVarlen32Type(getContext()));
                    Value rv = loadTyped(b2, l2, rhsRef, getVarlen32Type(getContext()));
                    Value cmp = applyStringCmp(b2, l2, predicate, lv, rv);
                    return asNullable(b2, l2, resultType, cmp, constBool(b2, l2, false));
                }},
                {lp.isRDFNode, [&](OpBuilder& b2, Location l2) -> Value {
                    Value raw = rt::VariantRuntime::compareNodeRefRef(b2, l2)({lhsRef, rhsRef, predConst})[0];
                    return packFromTriBool(b2, l2, resultType, raw);
                }},
                {lp.isUnspecified, [&](OpBuilder& b2, Location l2) -> Value {
                    return nullValue(b2, l2, resultType);
                }},
            };
            return dispatchOnTag(b, l, cases, blobLiteralCmp);
        };

        auto outer = rewriter.create<scf::IfOp>(
            loc, sameTag,
            [&](OpBuilder& b, Location l) { b.create<scf::YieldOp>(l, dispatchOnTag(b, l, fastCases, sameTagSlow)); },
            [&](OpBuilder& b, Location l) {
                Value bothNumeric = b.create<arith::AndIOp>(l, lp.isNumericFamily, rp.isNumericFamily);
                std::vector<TagCase> crossTagCases = {{bothNumeric, numericCrossCmp}};
                Value result = dispatchOnTag(b, l, crossTagCases, [&](OpBuilder& b2, Location l2) -> Value {
                    Value neitherScratch = b2.create<arith::AndIOp>(l2, notB(b2, l2, lp.isScratchPayload), notB(b2, l2, rp.isScratchPayload));
                    std::vector<TagCase> literalCase = {{neitherScratch, blobLiteralCmp}};
                    return dispatchOnTag(b2, l2, literalCase, [&](OpBuilder& b3, Location l3) -> Value {
                        return nullValue(b3, l3, resultType);
                    });
                });
                b.create<scf::YieldOp>(l, result);
            });
        rewriter.replaceOp(op, outer.getResult(0));
        return success();
    }
};

class OrderOpLowering : public OpConversionPattern<variant::OrderOp> {
    public:
    using OpConversionPattern<variant::OrderOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::OrderOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto [lhsTag, lhsRef] = unpackVariant(rewriter, loc, adaptor.getLhs());
        auto [rhsTag, rhsRef] = unpackVariant(rewriter, loc, adaptor.getRhs());
        Value lhsPayload = rewriter.create<util::PtrToIntOp>(loc, rewriter.getI64Type(), lhsRef);
        Value rhsPayload = rewriter.create<util::PtrToIntOp>(loc, rewriter.getI64Type(), rhsRef);
        Value result = rt::VariantRuntime::compareOrder(rewriter, loc)({lhsPayload, lhsTag, lhsRef, rhsPayload, rhsTag, rhsRef})[0];
        rewriter.replaceOp(op, result);
        return success();
    }
};

class PredicateOpLowering : public OpConversionPattern<variant::PredicateOp> {
    public:
    using OpConversionPattern<variant::PredicateOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(variant::PredicateOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        if (!mlir::isa<db::NullableType>(op.getResult().getType())) return rewriter.notifyMatchFailure(op, "variant.predicate result must be !db.nullable<i1>");
        auto resultType = op.getResult().getType();
        auto predArr = op.getPred();
        if (predArr.empty()) return rewriter.notifyMatchFailure(op, "variant.predicate's `pred` array must start with the predicate function name");
        auto fnAttr = mlir::dyn_cast<mlir::StringAttr>(predArr[0]);
        if (!fnAttr) return rewriter.notifyMatchFailure(op, "variant.predicate's `pred` array must start with a string (the function name)");
        llvm::StringRef fn = fnAttr.getValue();
        auto [tag, ref] = unpackVariant(rewriter, loc, adaptor.getVar());
        if (fn == "BOUND") {
            Value unspecifiedTag = constI32(rewriter, loc, xsd::to_int32(xsd::Type::Unspecified));
            Value isBound = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::ne, tag, unspecifiedTag);
            rewriter.replaceOp(op, asNullable(rewriter, loc, resultType, isBound, constBool(rewriter, loc, false)));
            return success();
        }
        if (fn == "LANGMATCHES") {
            if (predArr.size() != 2) return rewriter.notifyMatchFailure(op, "variant.predicate LANGMATCHES requires exactly one argument (the language range)");
            auto rangeAttr = mlir::dyn_cast<mlir::StringAttr>(predArr[1]);
            if (!rangeAttr) return rewriter.notifyMatchFailure(op, "variant.predicate LANGMATCHES's language range must be a string");
            Value payload = rewriter.create<util::PtrToIntOp>(loc, rewriter.getI64Type(), ref);
            Value range = rewriter.create<util::CreateConstVarLen>(loc, getVarlen32Type(getContext()), rangeAttr);
            Value raw = rt::VariantRuntime::langMatches(rewriter, loc)({payload, tag, ref, range})[0];
            rewriter.replaceOp(op, packFromTriBool(rewriter, loc, resultType, raw));
            return success();
        }
        return rewriter.notifyMatchFailure(op, llvm::Twine("variant.predicate: unsupported predicate function '") + fn + "'");
    }
};

Value applyIntArith(OpBuilder& b, Location loc, variant::VariantArithPredicate p, Value lhs, Value rhs, bool isUnsigned) {
    switch (p) {
        case variant::VariantArithPredicate::add: return b.create<arith::AddIOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::sub: return b.create<arith::SubIOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::mul: return b.create<arith::MulIOp>(loc, lhs, rhs);
        case variant::VariantArithPredicate::div:
            return isUnsigned ? b.create<arith::DivUIOp>(loc, lhs, rhs).getResult() : b.create<arith::DivSIOp>(loc, lhs, rhs).getResult();
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
        auto [lhsTag, lhsRef] = unpackVariant(rewriter, loc, adaptor.getLhs());
        auto [rhsTag, rhsRef] = unpackVariant(rewriter, loc, adaptor.getRhs());
        Value sameTag = rewriter.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, lhsTag, rhsTag);
        auto predicate = op.getPredicate();

        // Runtime-call fallback via rdf4cpp API
        auto slowArith = [&](OpBuilder& b, Location l) -> Value {
            Value predConst = constI32(b, l, static_cast<int32_t>(predicate));
            Value lhsPayload = b.create<util::PtrToIntOp>(l, b.getI64Type(), lhsRef);
            Value rhsPayload = b.create<util::PtrToIntOp>(l, b.getI64Type(), rhsRef);
            Value outSlot = allocStack(rewriter, op, b.getIntegerType(8), 8);
            Value resultTag = rt::VariantRuntime::arithNumericCross(b, l)({lhsPayload, lhsTag, rhsPayload, rhsTag, predConst, outSlot})[0];
            return packVariant(b, l, resultTag, inlineFromScratch(b, l, outSlot));
        };

        // Same-tag fast paths for fixed sized scalars up to 8 bytes.
        std::vector<TagCase> fastCases;
        for (const FixedNumericTag& n : fixedNumericTags()) {
            if (!n.isFloat && predicate == variant::VariantArithPredicate::div) continue;
            Value isThisTag = getEqTag(rewriter, loc, lhsTag, n.type);
            fastCases.push_back({isThisTag, [&, n](OpBuilder& b, Location l) -> Value {
                mlir::Type ty = fixedNumericMlirType(b, n);
                Value lv = unpackInline(b, l, lhsRef, ty);
                Value rv = unpackInline(b, l, rhsRef, ty);
                Value res = n.isFloat ? applyFloatArith(b, l, predicate, lv, rv) : applyIntArith(b, l, predicate, lv, rv, n.isUnsigned);
                Value resTag = constI32(b, l, xsd::to_int32(n.type));
                    return packVariant(b, l, resTag, packInline(b, l, res));
            }});
        }

        auto result = rewriter.create<scf::IfOp>(
            loc, sameTag,
            [&](OpBuilder& b, Location l) { b.create<scf::YieldOp>(l, dispatchOnTag(b, l, fastCases, slowArith)); },
            [&](OpBuilder& b, Location l) { b.create<scf::YieldOp>(l, slowArith(b, l)); });
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
        typeConverter.addTargetMaterialization([](OpBuilder& builder, mlir::Type resultType, mlir::ValueRange inputs, mlir::Location loc) -> mlir::Value {
            if (inputs.size() != 1) return nullptr;
            return builder.create<mlir::UnrealizedConversionCastOp>(loc, resultType, inputs).getResult(0);
        });

        RewritePatternSet patterns(ctxt);
        patterns.insert<CreateScalarOpLowering>(typeConverter, ctxt);
        patterns.insert<CreateNodeRefOpLowering>(typeConverter, ctxt);
        patterns.insert<UnspecifiedIfOpLowering>(typeConverter, ctxt);
        patterns.insert<VariantIsAOpLowering>(typeConverter, ctxt);
        patterns.insert<VariantGetValOpLowering>(typeConverter, ctxt);
        patterns.insert<ToStringOpLowering>(typeConverter, ctxt);
        patterns.insert<StrCastOpLowering>(typeConverter, ctxt);
        patterns.insert<LangCastOpLowering>(typeConverter, ctxt, PatternBenefit(2));
        patterns.insert<CastOpLowering>(typeConverter, ctxt);
        patterns.insert<CmpOpLowering>(typeConverter, ctxt);
        patterns.insert<OrderOpLowering>(typeConverter, ctxt);
        patterns.insert<ArithOpLowering>(typeConverter, ctxt);
        patterns.insert<PredicateOpLowering>(typeConverter, ctxt);

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
