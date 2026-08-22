#include "lingodb/execution/QueryCache.h"

#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/execution/LLVMBackends.h"
#include "lingodb/runtime/ExecutionContext.h"
#include "lingodb/utility/Setting.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"

#include "mlir/IR/OwningOpRef.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {
lingodb::utility::GlobalSetting<bool> cacheDebugSetting("system.cache.debug", false);
// Empty by default: on-disk persistence (surviving a process restart) is opt-in on top
// of system.cache.enable, not implied by it -- a bare `system.cache.enable=true` still
// gets the in-process-only cache exactly as before.
lingodb::utility::GlobalSetting<std::string> cacheDirSetting("system.cache.dir", "");
lingodb::utility::GlobalSetting<int64_t> cacheMaxSizeBytesSetting("system.cache.max_size_bytes", int64_t{1} << 30);
} // namespace

using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;

namespace {

void writeI32(std::vector<uint8_t>& buffer, size_t id, int32_t value) {
   std::memcpy(buffer.data() + id * lingodb::execution::kQueryParamSlotBytes, &value, sizeof(value));
}

void writeVarLen32(std::vector<uint8_t>& buffer, size_t id, llvm::StringRef str, lingodb::runtime::ExecutionContext& executionContext) {
   size_t len = str.size();
   uint64_t first4 = 0;
   std::memcpy(&first4, str.data(), std::min<size_t>(4, len));
   uint64_t low = (first4 << 32) | static_cast<uint32_t>(len);
   uint64_t high = 0;
   if (len <= 12) {
      if (len > 4) {
         std::memcpy(&high, str.data() + 4, std::min<size_t>(8, len - 4));
      }
   } 
   else {
      uint8_t* storage = executionContext.allocLiteral(len);
      std::memcpy(storage, str.data(), len);
      high = reinterpret_cast<uint64_t>(storage);
   }
   uint8_t* slot = buffer.data() + id * lingodb::execution::kQueryParamSlotBytes;
   std::memcpy(slot, &low, sizeof(low));
   std::memcpy(slot + sizeof(low), &high, sizeof(high));
}

void writeLiteral(std::vector<uint8_t>& buffer, size_t id, const relalg::QueryParamLiteral& literal, lingodb::runtime::ExecutionContext& executionContext) {
   uint8_t* slot = buffer.data() + id * lingodb::execution::kQueryParamSlotBytes;
   if (auto intType = mlir::dyn_cast<mlir::IntegerType>(literal.type)) {
      auto bits = mlir::cast<mlir::IntegerAttr>(literal.value).getValue().sextOrTrunc(64).getZExtValue();
      auto nbytes = std::min<size_t>((intType.getWidth() + 7) / 8, 8);
      std::memcpy(slot, &bits, nbytes);
   } 
   else if (mlir::isa<mlir::Float32Type>(literal.type)) {
      float f = static_cast<float>(mlir::cast<mlir::FloatAttr>(literal.value).getValueAsDouble());
      std::memcpy(slot, &f, sizeof(f));
   } 
   else if (mlir::isa<mlir::Float64Type>(literal.type)) {
      double d = mlir::cast<mlir::FloatAttr>(literal.value).getValueAsDouble();
      std::memcpy(slot, &d, sizeof(d));
   } 
   else if (mlir::isa<db::StringType>(literal.type)) {
      writeVarLen32(buffer, id, mlir::cast<mlir::StringAttr>(literal.value).getValue(), executionContext);
   } 
   else {
      assert(false && "buildQueryParamBuffer: unsupported cacheable literal type -- isCacheableQueryParamType in OperatorInterfaceImpl.cpp accepted a type this function doesn't know how to encode");
   }
}

mlir::Attribute termForSlot(gpm::TriplePatternOp op, gpm::TripleSlot slot) {
   switch (slot) {
      case gpm::TripleSlot::subject: return op.getS();
      case gpm::TripleSlot::predicate: return op.getP();
      case gpm::TripleSlot::object: return op.getO();
   }
   llvm_unreachable("unhandled TripleSlot");
}

} // namespace

std::vector<uint8_t> lingodb::execution::buildQueryParamBuffer(mlir::ModuleOp markedModule, runtime::ExecutionContext& executionContext) {
   size_t numParams = 0;
   if (auto countAttr = markedModule->getAttrOfType<mlir::IntegerAttr>(relalg::kQueryParamCountAttrName)) {
      numParams = static_cast<size_t>(countAttr.getInt());
   }
   std::vector<uint8_t> buffer(numParams * kQueryParamSlotBytes, 0);
   if (numParams == 0) return buffer;

   auto& namedGraphManager = markedModule.getContext()->getLoadedDialect<gsubop::GraphSubOpDialect>()->getNamedGraphManager();

   markedModule.walk([&](Parameterizable op) {
      auto paramsAttr = op->getAttrOfType<mlir::ArrayAttr>(relalg::kQueryParamsAttrName);
      if (!paramsAttr) return;

      if (auto triple = mlir::dyn_cast<gpm::TriplePatternOp>(op.getOperation())) {
         auto refType = mlir::cast<gpm::GraphReferenceType>(triple.getGraphRef().getColumn().type);
         std::string graphName = refType.getName().str();
         for (auto slot : {gpm::TripleSlot::subject, gpm::TripleSlot::predicate, gpm::TripleSlot::object}) {
            auto id = triple.getParamId(slot);
            if (!id) continue;
            auto ident = mlir::cast<gpm::IdentifierTermAttr>(termForSlot(triple, slot));
            int32_t resolved = namedGraphManager.resolve(graphName, ident.identifier());
            writeI32(buffer, *id, resolved);
         }
         return;
      }
      auto literals = op.getParamLiterals();
      assert(literals.size() == paramsAttr.size() && "buildQueryParamBuffer: predicate region shape changed unexpectedly since marking");
      for (auto [i, entry] : llvm::enumerate(paramsAttr)) {
         auto dict = mlir::cast<mlir::DictionaryAttr>(entry);
         auto id = static_cast<size_t>(mlir::cast<mlir::IntegerAttr>(dict.get(relalg::kQueryParamIdKey)).getInt());
         writeLiteral(buffer, id, literals[i], executionContext);
      }
   });
   return buffer;
}

namespace {
void maskParamsAttrForCacheKey(mlir::Operation* op) {
   auto paramsAttr = op->getAttrOfType<mlir::ArrayAttr>(relalg::kQueryParamsAttrName);
   if (!paramsAttr) return;
   llvm::SmallVector<mlir::Attribute> masked;
   masked.reserve(paramsAttr.size());
   for (auto entry : paramsAttr) {
      auto dict = mlir::cast<mlir::DictionaryAttr>(entry);
      mlir::NamedAttribute idAttr(mlir::StringAttr::get(op->getContext(), relalg::kQueryParamIdKey), dict.get(relalg::kQueryParamIdKey));
      masked.push_back(mlir::DictionaryAttr::get(op->getContext(), {idAttr}));
   }
   op->setAttr(relalg::kQueryParamsAttrName, mlir::ArrayAttr::get(op->getContext(), masked));
}
} // namespace

std::string lingodb::execution::computeQueryCacheKey(mlir::ModuleOp markedModule) {
   mlir::OwningOpRef<mlir::ModuleOp> clone(mlir::cast<mlir::ModuleOp>(markedModule->clone()));
   clone->walk([](Parameterizable op) {
      op.maskParameters();
      maskParamsAttrForCacheKey(op.getOperation());
   });

   std::string moduleText;
   {
      llvm::raw_string_ostream os(moduleText);
      mlir::OpPrintingFlags flags;
      flags.printGenericOpForm(false);
      clone->print(os, flags);
   }
   if (cacheDebugSetting.getValue()) {
      llvm::errs() << "[query-cache] masked module for key computation:\n"
                   << moduleText << "\n";
   }

   moduleText += "\n#codegen-cache-stamp llvm=";
   moduleText += LLVM_VERSION_STRING;
   moduleText += " target=";
   moduleText += llvm::sys::getDefaultTargetTriple();

   auto digest = llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(moduleText.data()), moduleText.size()));
   static constexpr char hexDigits[] = "0123456789abcdef";
   std::string hex;
   hex.reserve(digest.size() * 2);
   for (uint8_t byte : digest) {
      hex.push_back(hexDigits[byte >> 4]);
      hex.push_back(hexDigits[byte & 0xF]);
   }
   if (cacheDebugSetting.getValue()) {
      llvm::errs() << "[query-cache] key=" << hex << "\n";
   }
   return hex;
}

namespace {
namespace fs = std::filesystem;

std::string objectFilePath(const std::string& dir, const std::string& key) {
   return dir + "/" + key + ".o";
}

// Removes oldest-mtime `*.o` files in `dir` until its total size is back at or under
// `maxBytes`. mtime reflects when an entry was written (store() writes a fresh file, it
// never rewrites an existing one on a lookup hit), so this is an approximation of LRU --
// "least recently produced" rather than "least recently used" -- deliberately: touching
// every file's mtime on every disk-cache hit would put a filesystem write on the hot
// path of what's supposed to be the fast path. Best-effort: a filesystem error on any
// one file (concurrent eviction by a peer process, permissions, ...) just skips that
// file rather than aborting the sweep.
void evictLRU(const std::string& dir, int64_t maxBytes) {
   std::error_code ec;
   if (!fs::is_directory(dir, ec)) return;

   struct Entry {
      fs::path path;
      int64_t size;
      fs::file_time_type mtime;
   };
   std::vector<Entry> entries;
   int64_t total = 0;
   // A plain range-for over directory_iterator(dir, ec) would still call the
   // throwing operator++ internally (the error_code constructor only makes
   // construction/dereference non-throwing) -- a real risk here since this directory is
   // written concurrently by peer processes/threads doing their own store()/evictLRU().
   // Iterate and increment manually instead, and give up on the whole sweep (best-effort,
   // see comment above) rather than let a mid-sweep race surface as an exception.
   try {
      for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
         const auto& de = *it;
         if (de.path().extension() != ".o") continue;
         std::error_code entryEc;
         auto size = static_cast<int64_t>(de.file_size(entryEc));
         if (entryEc) continue;
         auto mtime = de.last_write_time(entryEc);
         if (entryEc) continue;
         entries.push_back({de.path(), size, mtime});
         total += size;
      }
   } catch (const fs::filesystem_error&) {
      return;
   }
   if (total <= maxBytes) return;

   std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
   for (auto& entry : entries) {
      if (total <= maxBytes) break;
      std::error_code removeEc;
      if (fs::remove(entry.path, removeEc)) total -= entry.size;
   }
}

// Writes via a same-directory temp file + atomic rename so a concurrent lookup() in
// another process (or thread) never observes a partially-written object file.
void writeObjectFileAtomically(const std::string& path, const std::vector<uint8_t>& bytes) {
   std::string tmpPath = path + ".tmp" + std::to_string(::getpid());
   {
      std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
      if (!out) return;
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
   }
   std::error_code renameEc;
   fs::rename(tmpPath, path, renameEc);
   if (renameEc) {
      std::error_code removeEc;
      fs::remove(tmpPath, removeEc);
   }
}
} // namespace

lingodb::execution::QueryCache& lingodb::execution::QueryCache::instance() {
   static QueryCache cache;
   return cache;
}

std::optional<lingodb::execution::CachedCompiledQuery> lingodb::execution::QueryCache::lookup(const std::string& key) {
   {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = entries.find(key);
      if (it != entries.end()) return it->second;
   }
   auto dir = cacheDirSetting.getValue();
   if (dir.empty()) return std::nullopt;
   auto path = objectFilePath(dir, key);
   std::error_code existsEc;
   if (!fs::is_regular_file(path, existsEc)) return std::nullopt;

   auto fileOrErr = llvm::MemoryBuffer::getFile(path);
   if (!fileOrErr) return std::nullopt;
   std::vector<uint8_t> bytes((*fileOrErr)->getBufferStart(), (*fileOrErr)->getBufferEnd());

   auto loaded = loadCachedObjectFromBytes(bytes);
   if (!loaded) return std::nullopt;
   if (cacheDebugSetting.getValue()) {
      llvm::errs() << "[query-cache] loaded key=" << key << " from disk (" << bytes.size() << " bytes)\n";
   }

   std::lock_guard<std::mutex> lock(mutex);
   return entries.emplace(key, std::move(*loaded)).first->second;
}

void lingodb::execution::QueryCache::store(std::string key, CachedCompiledQuery entry) {
   auto dir = cacheDirSetting.getValue();
   if (!dir.empty() && !entry.objectBytes.empty()) {
      std::error_code mkdirEc;
      fs::create_directories(dir, mkdirEc);
      if (!mkdirEc) {
         writeObjectFileAtomically(objectFilePath(dir, key), entry.objectBytes);
         evictLRU(dir, cacheMaxSizeBytesSetting.getValue());
      }
      // Already durable on disk (or we gave up trying); no need to also keep a second
      // copy of the raw bytes pinned in the in-memory entry below.
      entry.objectBytes.clear();
      entry.objectBytes.shrink_to_fit();
   }

   std::lock_guard<std::mutex> lock(mutex);
   entries.emplace(std::move(key), std::move(entry));
}
