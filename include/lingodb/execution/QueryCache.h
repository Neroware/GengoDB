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

// Process-lifetime cache of compiled queries, keyed by computeQueryCacheKey(). Backed by
// an in-memory map plus, when `system.cache.dir` is set, an on-disk `<key>.o` per entry
// so a cache built up by one process's compiles can be reused after a restart -- store()
// persists the entry's object bytes (see CachedCompiledQuery::objectBytes) to disk and
// evicts old entries once `system.cache.max_size_bytes` is exceeded; lookup() falls back
// to disk and links a found `.o` back into a callable function (loadCachedObjectFromBytes,
// LLVMBackends.h) when the in-memory map doesn't have the key -- e.g. right after a
// process restart, or on the first request a peer process's compile satisfies.
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
