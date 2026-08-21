#ifndef LINGODB_COMPILER_DIALECT_RELALG_TRANSFORMS_QUERYPARAMETERS_H
#define LINGODB_COMPILER_DIALECT_RELALG_TRANSFORMS_QUERYPARAMETERS_H
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Region.h"

#include <vector>

namespace lingodb::compiler::dialect::relalg {

struct QueryParamLiteral {
   mlir::Attribute value;
   mlir::Type type;
};

constexpr llvm::StringLiteral kQueryParamsAttrName = "params";
constexpr llvm::StringLiteral kQueryParamIdKey = "id";
constexpr llvm::StringLiteral kQueryParamValueKey = "value";

struct QueryParameter {
   size_t id;
   mlir::Attribute originalValue;
};

mlir::ArrayAttr makeQueryParamsAttr(mlir::MLIRContext* ctxt, size_t id, mlir::Attribute value);
void forwardParameter(mlir::Operation* from, size_t id, mlir::Operation* to, mlir::Attribute newValue = {});
std::vector<QueryParameter> collectQueryParameters(mlir::ModuleOp moduleOp);

} // namespace lingodb::compiler::dialect::relalg

#endif //LINGODB_COMPILER_DIALECT_RELALG_TRANSFORMS_QUERYPARAMETERS_H
