#ifndef LINGODB_EXECUTION_QUERYCACHE_H
#define LINGODB_EXECUTION_QUERYCACHE_H

#include "mlir/IR/BuiltinOps.h"

#include <cstdint>
#include <vector>

namespace lingodb::runtime {
class ExecutionContext;
} // namespace lingodb::runtime

namespace lingodb::execution {

constexpr size_t kQueryParamSlotBytes = 16;

std::vector<uint8_t> buildQueryParamBuffer(mlir::ModuleOp markedModule, runtime::ExecutionContext& executionContext);

} // namespace lingodb::execution

#endif //LINGODB_EXECUTION_QUERYCACHE_H
