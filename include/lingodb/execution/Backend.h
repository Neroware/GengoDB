#ifndef LINGODB_EXECUTION_BACKEND_H
#define LINGODB_EXECUTION_BACKEND_H
#include "Error.h"
#include "Instrumentation.h"
#include "lingodb/runtime/ExecutionContext.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
namespace mlir {
class ModuleOp;
} // namespace mlir
namespace lingodb::execution {
using mainFnType = std::add_pointer<void()>::type;

struct CachedCompiledQuery {
   mainFnType mainFunc = nullptr;
   std::shared_ptr<void> keepAlive;
   // Raw object-code bytes for this compiled query, non-empty only when produced by a
   // backend that supports on-disk persistence (currently DefaultCPULLVMBackend only).
   // QueryCache::store() persists these to <cache.dir>/<key>.o when disk caching is
   // configured; left empty for entries loaded back from disk (nothing to re-persist).
   std::vector<uint8_t> objectBytes;
};

class ExecutionBackend {
   protected:
   std::unordered_map<std::string, double> timing;
   Error error;
   bool verify = true;
   std::shared_ptr<SnapshotState> serializationState;
   std::optional<CachedCompiledQuery> cachedQuery;

   public:
   const std::unordered_map<std::string, double>& getTiming() const {
      return timing;
   }
   Error& getError() { return error; }
   void disableVerification() {
      verify = false;
   }
   virtual bool isLLVMBased() const { return true; }
   virtual void execute(mlir::ModuleOp& moduleOp, runtime::ExecutionContext* executionContext) = 0;
   void setSerializationState(std::shared_ptr<SnapshotState> serializationState) {
      ExecutionBackend::serializationState = serializationState;
   }
   auto getSerializationState() {
      return serializationState;
   }
   std::optional<CachedCompiledQuery> takeCachedQuery() {
      return std::exchange(cachedQuery, std::nullopt);
   }

   virtual ~ExecutionBackend() {}
};
void visitBareFunctions(const std::function<void(std::string, void*)>& fn);
} // namespace lingodb::execution
#endif //LINGODB_EXECUTION_BACKEND_H
