#ifndef LINGODB_EXECUTION_QUERYCACHE_H
#define LINGODB_EXECUTION_QUERYCACHE_H

#include "lingodb/execution/Backend.h"

#include "mlir/IR/BuiltinOps.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lingodb::runtime {
class ExecutionContext;
} // namespace lingodb::runtime

namespace lingodb::execution {

// Byte size of one slot in the codegen-cache parameter buffer.
constexpr size_t kQueryParamSlotBytes = 16;

std::vector<uint8_t> buildQueryParamBuffer(mlir::ModuleOp markedModule, runtime::ExecutionContext& executionContext);
std::string computeQueryCacheKey(mlir::ModuleOp markedModule);

class QueryCache {
   public:
   static QueryCache& instance();
   std::optional<CachedCompiledQuery> lookup(const std::string& key);
   void store(std::string key, CachedCompiledQuery entry);

   private:
   std::mutex mutex;
   std::unordered_map<std::string, CachedCompiledQuery> entries;
};

} // namespace lingodb::execution

#endif //LINGODB_EXECUTION_QUERYCACHE_H
