#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Passes.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"

namespace {
using namespace lingodb::compiler::dialect;

class ParameterizeQuery : public mlir::PassWrapper<ParameterizeQuery, mlir::OperationPass<mlir::ModuleOp>> {
    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ParameterizeQuery)
    llvm::StringRef getArgument() const override { return "relalg-parameterize"; }

    void runOnOperation() override {
        auto moduleOp = getOperation();
        size_t nextId = 0;
        moduleOp.walk([&](Parameterizable op) {
            auto literals = op.getParamLiterals();
            if (literals.empty()) return;
            llvm::SmallVector<mlir::Attribute> entries;
            for (auto& literal : literals) {
                mlir::Builder builder(op.getContext());
                mlir::NamedAttribute idAttr(builder.getStringAttr(relalg::kQueryParamIdKey), builder.getI64IntegerAttr(static_cast<int64_t>(nextId)));
                mlir::NamedAttribute valueAttr(builder.getStringAttr(relalg::kQueryParamValueKey), literal.value);
                entries.push_back(mlir::DictionaryAttr::get(op.getContext(), {idAttr, valueAttr}));
                ++nextId;
            }
            op->setAttr(relalg::kQueryParamsAttrName, mlir::ArrayAttr::get(op.getContext(), entries));
        });
        mlir::Builder builder(moduleOp.getContext());
        moduleOp->setAttr(relalg::kQueryParamCountAttrName, builder.getI64IntegerAttr(static_cast<int64_t>(nextId)));
    }
};
} // namespace

std::unique_ptr<mlir::Pass> relalg::createParameterizeQueryPass() {
   return std::make_unique<ParameterizeQuery>();
}
