#include "gengodb/compiler/Conversion/XSDToControlFlow/XSDToControlFlowPass.h"

#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamDialect.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/XSD/XSDDialect.h"
#include "gengodb/compiler/Dialect/XSD/XSDOps.h"
#include "lingodb/gengodb/runtime/XsdRuntime.h"

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
namespace rt = lingodb::compiler::runtime;

mlir::Value unwrapNullableVarLen(mlir::OpBuilder& b, mlir::Location loc, mlir::Value nullableVarLen) {
    return b.create<db::NullableGetVal>(loc, util::VarLen32Type::get(nullableVarLen.getContext()), nullableVarLen);
}
mlir::Value xsdTriStateToNullableBool(mlir::OpBuilder& b, mlir::Location loc, mlir::Value raw, mlir::Type resultType) {
    mlir::Value minusOne = b.create<arith::ConstantOp>(loc, b.getI8Type(), b.getIntegerAttr(b.getI8Type(), -1));
    mlir::Value one = b.create<arith::ConstantOp>(loc, b.getI8Type(), b.getIntegerAttr(b.getI8Type(), 1));
    mlir::Value isError = b.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, raw, minusOne);
    mlir::Value boolVal = b.create<arith::CmpIOp>(loc, arith::CmpIPredicate::eq, raw, one);
    return b.create<db::AsNullableOp>(loc, resultType, boolVal, isError);
}
static mlir::Value asOpaqueRuntimeRef(mlir::OpBuilder& b, mlir::Location loc, mlir::Value ref) {
    auto opaqueType = util::RefType::get(ref.getContext(), b.getIntegerType(8));
    return b.create<mlir::UnrealizedConversionCastOp>(loc, opaqueType, ref).getResult(0);
}

class CompareLowering : public OpConversionPattern<xsd::CompareOp> {
    public:
    using OpConversionPattern<xsd::CompareOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(xsd::CompareOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto resultType = op.getResult().getType();
        mlir::Value predicateConst = rewriter.create<mlir::arith::ConstantIntOp>(loc, static_cast<int32_t>(op.getPredicate()), 32);
        mlir::Value lhsRef = asOpaqueRuntimeRef(rewriter, loc, adaptor.getLhs());
        mlir::Value rhsRef = asOpaqueRuntimeRef(rewriter, loc, adaptor.getRhs());
        mlir::Value lhsValid = rewriter.create<util::IsRefValidOp>(loc, rewriter.getI1Type(), lhsRef);
        mlir::Value rhsValid = rewriter.create<util::IsRefValidOp>(loc, rewriter.getI1Type(), rhsRef);
        mlir::Value bothValid = rewriter.create<arith::AndIOp>(loc, lhsValid, rhsValid);
        auto res = rewriter.create<mlir::scf::IfOp>(loc, bothValid,
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value raw = rt::XsdRuntime::compareNodeNode(b, loc)({lhsRef, rhsRef, predicateConst})[0];
                b.create<mlir::scf::YieldOp>(loc, xsdTriStateToNullableBool(b, loc, raw, resultType));
            },
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                b.create<mlir::scf::YieldOp>(loc, b.create<db::NullOp>(loc, resultType).getResult());
            }).getResult(0);
        rewriter.replaceOp(op, res);
        return mlir::success();
    }
};

class CompareLiteralLowering : public OpConversionPattern<xsd::CompareLiteralOp> {
    public:
    using OpConversionPattern<xsd::CompareLiteralOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(xsd::CompareLiteralOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto* ctxt = op.getContext();
        auto resultType = op.getResult().getType();
        mlir::Value predicateConst = rewriter.create<mlir::arith::ConstantIntOp>(loc, static_cast<int32_t>(op.getPredicate()), 32);
        mlir::Value lhsRef = asOpaqueRuntimeRef(rewriter, loc, adaptor.getLhs());
        mlir::Value lhsValid = rewriter.create<util::IsRefValidOp>(loc, rewriter.getI1Type(), lhsRef);
        auto res = rewriter.create<mlir::scf::IfOp>(loc, lhsValid,
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value lexicalForm = b.create<util::CreateConstVarLen>(loc, util::VarLen32Type::get(ctxt), op.getRhsLexicalForm());
                mlir::Value xsdType = b.create<mlir::arith::ConstantIntOp>(loc, op.getRhsXsdType(), 32);
                mlir::Value raw = rt::XsdRuntime::compareNodeLiteral(b, loc)({lhsRef, lexicalForm, xsdType, predicateConst})[0];
                b.create<mlir::scf::YieldOp>(loc, xsdTriStateToNullableBool(b, loc, raw, resultType));
            },
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                b.create<mlir::scf::YieldOp>(loc, b.create<db::NullOp>(loc, resultType).getResult());
            }).getResult(0);
        rewriter.replaceOp(op, res);
        return mlir::success();
   }
};

class LiteralOfRefLowering : public OpConversionPattern<xsd::LiteralOfRefOp> {
    public:
    using OpConversionPattern<xsd::LiteralOfRefOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(xsd::LiteralOfRefOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto lexResultType = op.getLexicalForm().getType();
        auto typeResultType = op.getXsdType().getType();
        mlir::Value ref = asOpaqueRuntimeRef(rewriter, loc, adaptor.getRef());
        mlir::Value valid = rewriter.create<util::IsRefValidOp>(loc, rewriter.getI1Type(), ref);
        auto ifOp = rewriter.create<mlir::scf::IfOp>(loc, valid,
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value rawType = rt::XsdRuntime::literalTypeOfRef(b, loc)({ref})[0];
                mlir::Value unspecified = b.create<mlir::arith::ConstantIntOp>(loc, 0, 32);
                mlir::Value isError = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, rawType, unspecified);
                mlir::Value rawLex = rt::XsdRuntime::literalLexicalOfRef(b, loc)({ref})[0];
                mlir::Value lex = rawLex;
                mlir::Value lexNullable = b.create<db::AsNullableOp>(loc, lexResultType, lex, isError);
                mlir::Value typNullable = b.create<db::AsNullableOp>(loc, typeResultType, rawType, isError);
                b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange{lexNullable, typNullable});
            },
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value lex = b.create<db::NullOp>(loc, lexResultType);
                mlir::Value typ = b.create<db::NullOp>(loc, typeResultType);
                b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange{lex, typ});
            });
        rewriter.replaceOp(op, ifOp.getResults());
        return mlir::success();
    }
};

class ArithLowering : public OpConversionPattern<xsd::ArithOp> {
    public:
    using OpConversionPattern<xsd::ArithOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(xsd::ArithOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto lexResultType = op.getLexicalForm().getType();
        auto typeResultType = op.getXsdType().getType();
        mlir::Value predicateConst = rewriter.create<mlir::arith::ConstantIntOp>(loc, static_cast<int32_t>(op.getPredicate()), 32);
        mlir::Value lhsTypeNull = rewriter.create<db::IsNullOp>(loc, rewriter.getI1Type(), adaptor.getLhsType());
        mlir::Value rhsTypeNull = rewriter.create<db::IsNullOp>(loc, rewriter.getI1Type(), adaptor.getRhsType());
        mlir::Value anyNull = rewriter.create<mlir::arith::OrIOp>(loc, lhsTypeNull, rhsTypeNull);
        mlir::Value bothValid = rewriter.create<mlir::arith::XOrIOp>(loc, anyNull, rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 1));
        auto ifOp = rewriter.create<mlir::scf::IfOp>(loc, bothValid,
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value lhsRaw = unwrapNullableVarLen(b, loc, adaptor.getLhsLexical());
                mlir::Value rhsRaw = unwrapNullableVarLen(b, loc, adaptor.getRhsLexical());
                mlir::Value lhsType = b.create<db::NullableGetVal>(loc, b.getI32Type(), adaptor.getLhsType());
                mlir::Value rhsType = b.create<db::NullableGetVal>(loc, b.getI32Type(), adaptor.getRhsType());
                mlir::Value rawResType = rt::XsdRuntime::arithType(b, loc)({lhsRaw, lhsType, rhsRaw, rhsType, predicateConst})[0];
                mlir::Value rawResLex = rt::XsdRuntime::arithLexical(b, loc)({lhsRaw, lhsType, rhsRaw, rhsType, predicateConst})[0];
                mlir::Value unspecified = b.create<mlir::arith::ConstantIntOp>(loc, 0, 32);
                mlir::Value isError = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, rawResType, unspecified);
                mlir::Value lex = rawResLex;
                mlir::Value lexNullable = b.create<db::AsNullableOp>(loc, lexResultType, lex, isError);
                mlir::Value typNullable = b.create<db::AsNullableOp>(loc, typeResultType, rawResType, isError);
                b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange{lexNullable, typNullable});
            },
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value lex = b.create<db::NullOp>(loc, lexResultType);
                mlir::Value typ = b.create<db::NullOp>(loc, typeResultType);
                b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange{lex, typ});
            });
        rewriter.replaceOp(op, ifOp.getResults());
        return mlir::success();
   }
};

class NegateLowering : public OpConversionPattern<xsd::NegateOp> {
    public:
    using OpConversionPattern<xsd::NegateOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(xsd::NegateOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto lexResultType = op.getLexicalForm().getType();
        auto typeResultType = op.getXsdTypeResult().getType();
        mlir::Value valid = rewriter.create<mlir::arith::XOrIOp>(loc,
            rewriter.create<db::IsNullOp>(loc, rewriter.getI1Type(), adaptor.getXsdType()),
            rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 1));
        auto ifOp = rewriter.create<mlir::scf::IfOp>(loc, valid,
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value raw = unwrapNullableVarLen(b, loc, adaptor.getLexical());
                mlir::Value type = b.create<db::NullableGetVal>(loc, b.getI32Type(), adaptor.getXsdType());
                mlir::Value rawResType = rt::XsdRuntime::negateType(b, loc)({raw, type})[0];
                mlir::Value rawResLex = rt::XsdRuntime::negateLexical(b, loc)({raw, type})[0];
                mlir::Value unspecified = b.create<mlir::arith::ConstantIntOp>(loc, 0, 32);
                mlir::Value isError = b.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, rawResType, unspecified);
                mlir::Value lex = rawResLex;
                mlir::Value lexNullable = b.create<db::AsNullableOp>(loc, lexResultType, lex, isError);
                mlir::Value typNullable = b.create<db::AsNullableOp>(loc, typeResultType, rawResType, isError);
                b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange{lexNullable, typNullable});
            },
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value lex = b.create<db::NullOp>(loc, lexResultType);
                mlir::Value typ = b.create<db::NullOp>(loc, typeResultType);
                b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange{lex, typ});
            });
        rewriter.replaceOp(op, ifOp.getResults());
        return mlir::success();
   }
};

class CompareDynLowering : public OpConversionPattern<xsd::CompareDynOp> {
    public:
    using OpConversionPattern<xsd::CompareDynOp>::OpConversionPattern;
    LogicalResult matchAndRewrite(xsd::CompareDynOp op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
        auto loc = op->getLoc();
        auto resultType = op.getResult().getType();
        mlir::Value predicateConst = rewriter.create<mlir::arith::ConstantIntOp>(loc, static_cast<int32_t>(op.getPredicate()), 32);
        mlir::Value lhsTypeNull = rewriter.create<db::IsNullOp>(loc, rewriter.getI1Type(), adaptor.getLhsType());
        mlir::Value rhsTypeNull = rewriter.create<db::IsNullOp>(loc, rewriter.getI1Type(), adaptor.getRhsType());
        mlir::Value anyNull = rewriter.create<mlir::arith::OrIOp>(loc, lhsTypeNull, rhsTypeNull);
        mlir::Value bothValid = rewriter.create<mlir::arith::XOrIOp>(loc, anyNull, rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 1));
        auto res = rewriter.create<mlir::scf::IfOp>(loc, bothValid,
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                mlir::Value lhsRaw = unwrapNullableVarLen(b, loc, adaptor.getLhsLexical());
                mlir::Value rhsRaw = unwrapNullableVarLen(b, loc, adaptor.getRhsLexical());
                mlir::Value lhsType = b.create<db::NullableGetVal>(loc, b.getI32Type(), adaptor.getLhsType());
                mlir::Value rhsType = b.create<db::NullableGetVal>(loc, b.getI32Type(), adaptor.getRhsType());
                mlir::Value raw = rt::XsdRuntime::compareDyn(b, loc)({lhsRaw, lhsType, rhsRaw, rhsType, predicateConst})[0];
                b.create<mlir::scf::YieldOp>(loc, xsdTriStateToNullableBool(b, loc, raw, resultType));
            },
            [&](mlir::OpBuilder& b, mlir::Location loc) {
                b.create<mlir::scf::YieldOp>(loc, b.create<db::NullOp>(loc, resultType).getResult());
            }).getResult(0);
        rewriter.replaceOp(op, res);
        return mlir::success();
    }
};

struct XSDToControlFlowLoweringPass
    : public mlir::PassWrapper<XSDToControlFlowLoweringPass, OperationPass<mlir::ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(XSDToControlFlowLoweringPass)
    virtual llvm::StringRef getArgument() const override { return "lower-xsd-to-cf"; }

    void getDependentDialects(DialectRegistry& registry) const override {
        registry.insert<db::DBDialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithDialect, gsubop::GraphSubOpDialect>();
    }
    void runOnOperation() override {
        auto module = getOperation();
        getContext().getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(module);

        ConversionTarget target(getContext());
        target.addLegalOp<ModuleOp>();
        target.addLegalOp<UnrealizedConversionCastOp>();
        target.addIllegalDialect<xsd::XSDDialect>();
        target.addLegalDialect<db::DBDialect>();
        target.addLegalDialect<gsubop::GraphSubOpDialect>();
        target.addLegalDialect<subop::SubOperatorDialect>();
        target.addLegalDialect<tuples::TupleStreamDialect>();
        target.addLegalDialect<func::FuncDialect>();
        target.addLegalDialect<memref::MemRefDialect>();
        target.addLegalDialect<arith::ArithDialect>();
        target.addLegalDialect<cf::ControlFlowDialect>();
        target.addLegalDialect<scf::SCFDialect>();
        target.addLegalDialect<util::UtilDialect>();

        auto* ctxt = &getContext();
        RewritePatternSet patterns(ctxt);
        patterns.insert<CompareLowering>(ctxt);
        patterns.insert<CompareLiteralLowering>(ctxt);
        patterns.insert<LiteralOfRefLowering>(ctxt);
        patterns.insert<ArithLowering>(ctxt);
        patterns.insert<NegateLowering>(ctxt);
        patterns.insert<CompareDynLowering>(ctxt);

        if (failed(applyFullConversion(module, target, std::move(patterns)))) {
            signalPassFailure();
        }
    }
};
} // namespace

std::unique_ptr<mlir::Pass> xsd::createLowerXSDToControlFlowPass() {
   return std::make_unique<XSDToControlFlowLoweringPass>();
}
void xsd::registerXSDToControlFlowConversionPasses() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return xsd::createLowerXSDToControlFlowPass();
   });
}
