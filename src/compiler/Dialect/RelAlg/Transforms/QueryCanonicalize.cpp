#include "lingodb/compiler/Dialect/RelAlg/Passes.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"

namespace {
using namespace lingodb::compiler::dialect;

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

        auto mainFunc = mlir::dyn_cast_or_null<mlir::func::FuncOp>(moduleOp.lookupSymbol("main"));
        if (!mainFunc) {
            moduleOp->setAttr(relalg::kQueryCacheableAttrName, attrBuilder.getBoolAttr(false));
            return;
        }

        llvm::SmallVector<mlir::Operation*> orderedLeaves;
        llvm::SmallVector<mlir::Type> fieldTypes;
        orderedLeaves.reserve(numParams);
        fieldTypes.reserve(numParams);
        for (size_t id = 0; id < numParams; ++id) {
            mlir::Operation* leaf = leavesById[id].front();
            orderedLeaves.push_back(leaf);
            fieldTypes.push_back(leaf->getResult(0).getType());
        }
        auto tupleType = mlir::TupleType::get(ctxt, fieldTypes);
        auto paramRefType = util::RefType::get(ctxt, tupleType);

        auto newFnType = mlir::FunctionType::get(ctxt, {paramRefType}, mainFunc.getFunctionType().getResults());
        mainFunc.setFunctionType(newFnType);
        mlir::Block& entryBlock = mainFunc.getBody().front();
        mlir::BlockArgument paramArg = entryBlock.insertArgument((unsigned) 0, paramRefType, mainFunc.getLoc());

        for (size_t id = 0; id < numParams; ++id) {
            mlir::Operation* leaf = orderedLeaves[id];
            mlir::OpBuilder builder(leaf);
            auto elementPtr = builder.create<util::TupleElementPtrOp>(leaf->getLoc(), util::RefType::get(ctxt, fieldTypes[id]), paramArg, static_cast<int32_t>(id));
            auto loaded = builder.create<util::LoadOp>(leaf->getLoc(), elementPtr);
            leaf->getResult(0).replaceAllUsesWith(loaded.getResult());
            leaf->erase();
        }
    }
};
} // namespace

std::unique_ptr<mlir::Pass> relalg::createQueryCanonicalizePass() {
   return std::make_unique<QueryCanonicalize>();
}
