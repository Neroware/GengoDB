#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"

#include "lingodb/utility/Setting.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"

#include <algorithm>
#include <cassert>

using namespace lingodb::compiler::dialect::relalg;

namespace {
lingodb::utility::GlobalSetting<bool> queryCacheEnableSetting("system.cache.enable", false);
} // namespace

bool lingodb::compiler::dialect::relalg::isQueryCacheEnabled() {
    return queryCacheEnableSetting.getValue();
}

mlir::ArrayAttr lingodb::compiler::dialect::relalg::makeQueryParamsAttr(mlir::MLIRContext* ctxt, size_t id, mlir::Attribute value) {
    mlir::Builder builder(ctxt);
    mlir::NamedAttribute idAttr(builder.getStringAttr(kQueryParamIdKey), builder.getI64IntegerAttr(static_cast<int64_t>(id)));
    mlir::NamedAttribute valueAttr(builder.getStringAttr(kQueryParamValueKey), value);
    auto dict = mlir::DictionaryAttr::get(ctxt, {idAttr, valueAttr});
    return mlir::ArrayAttr::get(ctxt, {dict});
}

void lingodb::compiler::dialect::relalg::forwardParameter(mlir::Operation* from, size_t id, mlir::Operation* to, mlir::Attribute newValue) {
    auto paramsAttr = from->getAttrOfType<mlir::ArrayAttr>(kQueryParamsAttrName);
    assert(paramsAttr && "forwardParameter: `from` does not carry a params attribute");
    mlir::Attribute value;
    for (auto entry : paramsAttr) {
        auto dict = mlir::cast<mlir::DictionaryAttr>(entry);
        auto entryId = mlir::cast<mlir::IntegerAttr>(dict.get(kQueryParamIdKey)).getInt();
        if (static_cast<size_t>(entryId) == id) {
            value = dict.get(kQueryParamValueKey);
            break;
        }
    }
    assert(value && "forwardParameter: id not found in `from`'s params attribute");
    to->setAttr(kQueryParamsAttrName, makeQueryParamsAttr(to->getContext(), id, newValue ? newValue : value));
}

std::vector<QueryParameter> lingodb::compiler::dialect::relalg::collectQueryParameters(mlir::ModuleOp moduleOp) {
    std::vector<QueryParameter> result;
    moduleOp.walk([&](mlir::Operation* op) {
        auto paramsAttr = op->getAttrOfType<mlir::ArrayAttr>(kQueryParamsAttrName);
        if (!paramsAttr) return;
        for (auto entry : paramsAttr) {
            auto dict = mlir::cast<mlir::DictionaryAttr>(entry);
            auto id = mlir::cast<mlir::IntegerAttr>(dict.get(kQueryParamIdKey)).getInt();
            auto value = dict.get(kQueryParamValueKey);
            result.push_back(QueryParameter{static_cast<size_t>(id), value});
        }
    });
    std::sort(result.begin(), result.end(), [](const QueryParameter& a, const QueryParameter& b) { return a.id < b.id; });
    return result;
}
