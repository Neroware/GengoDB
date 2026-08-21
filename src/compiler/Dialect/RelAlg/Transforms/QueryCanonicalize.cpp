#include "lingodb/compiler/Dialect/RelAlg/Passes.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/compiler/Dialect/util/FunctionHelper.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"
#include "lingodb/compiler/runtime/ExecutionContext.h"

namespace {
using namespace lingodb::compiler::dialect;
namespace rt = lingodb::compiler::runtime;
class QueryCanonicalize : public mlir::PassWrapper<QueryCanonicalize, mlir::OperationPass<mlir::ModuleOp>> {
    public:
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(QueryCanonicalize)
    llvm::StringRef getArgument() const override { return "std-query-canonicalize"; }
    void getDependentDialects(mlir::DialectRegistry& registry) const override {
        registry.insert<util::UtilDialect>();
    }

    void runOnOperation() override {
        mlir::ModuleOp moduleOp = getOperation();
        auto* ctxt = moduleOp.getContext();
        ctxt->getLoadedDialect<util::UtilDialect>()->getFunctionHelper().setParentModule(moduleOp);

        size_t numParams = 0;
        if (auto countAttr = moduleOp->getAttrOfType<mlir::IntegerAttr>(relalg::kQueryParamCountAttrName)) {
            numParams = static_cast<size_t>(countAttr.getInt());
        }

        llvm::DenseMap<size_t, llvm::SmallVector<mlir::Operation*, 1>> leavesById;
        moduleOp.walk([&](mlir::Operation* op) {
            auto paramsAttr = op->getAttrOfType<mlir::ArrayAttr>(relalg::kQueryParamsAttrName);
            if (!paramsAttr) return;
            for (auto entry : paramsAttr) {
                auto dict = mlir::cast<mlir::DictionaryAttr>(entry);
                auto id = static_cast<size_t>(mlir::cast<mlir::IntegerAttr>(dict.get(relalg::kQueryParamIdKey)).getInt());
                leavesById[id].push_back(op);
            }
        });

        bool cacheable = true;
        for (size_t id = 0; id < numParams; ++id) {
            auto it = leavesById.find(id);
            bool ok = it != leavesById.end() && it->second.size() == 1 && it->second.front()->getNumResults() == 1;
            if (!ok) {
                cacheable = false;
                llvm::errs() << "std-query-canonicalize: query param id " << id
                            << (it == leavesById.end() ? " has no resolved leaf op"
                                                        : (it->second.size() != 1 ? " has ambiguous leaf ops" : " leaf op has unexpected result arity"))
                            << "; forwarding site was missed; marking this compilation not cacheable.\n";
            }
        }

        mlir::Builder attrBuilder(ctxt);
        moduleOp->setAttr(relalg::kQueryCacheableAttrName, attrBuilder.getBoolAttr(cacheable));
        if (!cacheable) return;

        llvm::SmallVector<mlir::Operation*> orderedLeaves;
        llvm::SmallVector<mlir::Type> fieldTypes;
        orderedLeaves.reserve(numParams);
        fieldTypes.reserve(numParams);
        for (size_t id = 0; id < numParams; ++id) {
            mlir::Operation* leaf = leavesById[id].front();
            orderedLeaves.push_back(leaf);
            fieldTypes.push_back(leaf->getResult(0).getType());
        }
        auto i128Type = mlir::IntegerType::get(ctxt, 128);
        llvm::SmallVector<mlir::Type> slotTypes(numParams, i128Type);
        auto slotTupleType = mlir::TupleType::get(ctxt, slotTypes);
        auto slotRefType = util::RefType::get(ctxt, slotTupleType);

        for (size_t id = 0; id < numParams; ++id) {
            mlir::Operation* leaf = orderedLeaves[id];
            auto loc = leaf->getLoc();
            mlir::OpBuilder builder(leaf);
            mlir::Value rawBuf = rt::ExecutionContext::getQueryParamBuffer(builder, loc)({})[0];
            auto slotBuf = builder.create<util::GenericMemrefCastOp>(loc, slotRefType, rawBuf);
            auto slotPtr = builder.create<util::TupleElementPtrOp>(loc, util::RefType::get(ctxt, i128Type), slotBuf, static_cast<int32_t>(id));
            auto realPtr = builder.create<util::GenericMemrefCastOp>(loc, util::RefType::get(ctxt, fieldTypes[id]), slotPtr);
            auto loaded = builder.create<util::LoadOp>(loc, realPtr);
            leaf->getResult(0).replaceAllUsesWith(loaded.getResult());
            leaf->erase();
        }
    }
};
} // namespace

std::unique_ptr<mlir::Pass> relalg::createQueryCanonicalizePass() {
   return std::make_unique<QueryCanonicalize>();
}
