#include "lingodb/execution/QueryCache.h"

#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/RelAlg/Transforms/QueryParameters.h"
#include "lingodb/runtime/ExecutionContext.h"
#include "lingodb/utility/Setting.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"

#include "mlir/IR/OwningOpRef.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <cassert>
#include <cstring>

namespace {
lingodb::utility::GlobalSetting<bool> cacheDebugSetting("system.cache.debug", false);
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

lingodb::execution::QueryCache& lingodb::execution::QueryCache::instance() {
   static QueryCache cache;
   return cache;
}

std::optional<lingodb::execution::CachedCompiledQuery> lingodb::execution::QueryCache::lookup(const std::string& key) {
   std::lock_guard<std::mutex> lock(mutex);
   auto it = entries.find(key);
   if (it == entries.end()) return std::nullopt;
   return it->second;
}

void lingodb::execution::QueryCache::store(std::string key, CachedCompiledQuery entry) {
   std::lock_guard<std::mutex> lock(mutex);
   entries.emplace(std::move(key), std::move(entry));
}
