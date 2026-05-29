#include "gengodb/runtime/GengoDBGraph.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace lingodb::runtime {

static std::string graphPath(const std::string& dbDir, const std::string& fileName) {
   return dbDir + "/db.neo4j." + fileName + ".dat";
}

// Binary layout on disk mirrors the in-memory packed arrays exactly:
//   [header]  uint32_t magic, uint32_t nodeCount, uint32_t relCount, uint32_t propCount
//   [nodes]   nodeCount × sizeof(PropertyGraph::NodeEntry) bytes
//   [rels]    relCount  × sizeof(PropertyGraph::RelEntry)  bytes
//   [props]   propCount × sizeof(PropRecord)               bytes
static constexpr uint32_t GRAPH_FILE_MAGIC = 0x47524150; // "GRAP"

void GengoDBGraph::flush() {
    if (dbDir_.empty() || fileName_.empty()) return;
    std::string path = graphPath(dbDir_, fileName_);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("GengoDBGraph::flush: cannot open " + path);

    uint32_t header[4] = {
        GRAPH_FILE_MAGIC,
        static_cast<uint32_t>(storage_.nodeHighWater()),
        static_cast<uint32_t>(storage_.relHighWater()),
        static_cast<uint32_t>(storage_.propHighWater()),
    };
    std::fwrite(header, sizeof(header), 1, f);
    std::fwrite(storage_.nodeStorePtr(),
        sizeof(PropertyGraph::NodeEntry),
        static_cast<size_t>(storage_.nodeHighWater()), f);
   std::fwrite(storage_.relStorePtr(),
        sizeof(PropertyGraph::RelEntry),
        static_cast<size_t>(storage_.relHighWater()), f);
   std::fwrite(storage_.propStorePtr(),
        sizeof(PropertyGraph::PropRecord),
        static_cast<size_t>(storage_.propHighWater()), f);
   std::fclose(f);
}
void GengoDBGraph::ensureLoaded() {
   if (loaded_) return;
   loaded_ = true;
   if (storage_.nodeHighWater() > 0) return; // already populated in-memory
   if (dbDir_.empty() || fileName_.empty()) return;

   std::string path = graphPath(dbDir_, fileName_);
   if (!std::filesystem::exists(path)) return;

   std::FILE* f = std::fopen(path.c_str(), "rb");
   if (!f) throw std::runtime_error("GengoDBGraph::ensureLoaded: cannot open " + path);

   uint32_t header[4];
   std::fread(header, sizeof(header), 1, f);
   if (header[0] != GRAPH_FILE_MAGIC) {
      std::fclose(f);
      throw std::runtime_error("GengoDBGraph::ensureLoaded: bad magic in " + path);
   }
   // Direct memcpy into the flat packed stores
   std::fread(storage_.nodeStorePtr(), sizeof(PropertyGraph::NodeEntry), header[1], f);
   std::fread(storage_.relStorePtr(), sizeof(PropertyGraph::RelEntry), header[2], f);
   std::fread(storage_.propStorePtr(), sizeof(PropertyGraph::PropRecord), header[3], f);
   std::fclose(f);
}
void GengoDBGraph::serialize(lingodb::utility::Serializer& serializer) const {
   serializer.writeProperty<std::string>(1, fileName_);
}
std::unique_ptr<GengoDBGraph> GengoDBGraph::deserialize(lingodb::utility::Deserializer& deserializer) {
   auto fileName = deserializer.readProperty<std::string>(1);
   return create(std::move(fileName));
}
std::unique_ptr<GengoDBGraph> GengoDBGraph::create(std::string name) {
   return std::make_unique<GengoDBGraph>(std::move(name));
}

} // namespace lingodb::runtime
