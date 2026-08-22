#ifndef LINGODB_EXECUTION_LLVMBACKENDS_H
#define LINGODB_EXECUTION_LLVMBACKENDS_H
#include "Backend.h"
namespace lingodb::execution {
std::unique_ptr<ExecutionBackend> createDefaultLLVMBackend(bool optimize = true);
std::unique_ptr<ExecutionBackend> createLLVMDebugBackend();
std::unique_ptr<ExecutionBackend> createLLVMProfilingBackend();
std::unique_ptr<ExecutionBackend> createGPULLVMBackend();

// Links raw native object-code bytes (as produced by DefaultCPULLVMBackend's
// SimpleObjectCache, see CachedCompiledQuery::objectBytes) into a fresh JIT dylib and
// looks up its "main" symbol. Used by QueryCache to rehydrate a disk-persisted cache
// entry, both within the process that wrote it and, more importantly, in a later
// process after a restart. Returns std::nullopt on any failure (malformed/incompatible
// object, missing "main", ...) -- callers should treat that exactly like a cache miss.
std::optional<CachedCompiledQuery> loadCachedObjectFromBytes(const std::vector<uint8_t>& objectBytes);
} // namespace lingodb::execution
#endif //LINGODB_EXECUTION_LLVMBACKENDS_H
