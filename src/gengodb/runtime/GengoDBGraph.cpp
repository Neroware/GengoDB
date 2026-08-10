#include "gengodb/runtime/GengoDBGraph.h"
#include "gengodb/runtime/GraphData.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace lingodb::runtime {

static std::string graphPath(const std::string& dbDir, const std::string& fileName) {
   return dbDir + "/db.neo4j." + fileName + ".dat";
}

// Binary layout on disk is the Neo4J packed format:
//   [header]  uint32_t magic, uint32_t nodeCount, uint32_t relCount, uint32_t propCount
//   [nodes]   nodeCount × sizeof(Neo4JGraph::NodeEntry) bytes  (15 bytes each, packed)
//   [rels]    relCount  × sizeof(Neo4JGraph::RelEntry)  bytes  (34 bytes each, packed)
//   [props]   propCount × sizeof(Neo4JGraph::PropRecord) bytes (21 bytes each, packed)
static constexpr uint32_t GRAPH_FILE_MAGIC = 0x47524150; // "GRAP"

GengoDBGraph::~GengoDBGraph() {
   if (storage_) {
      GraphStorage::remove(reinterpret_cast<const uint8_t*>(storage_));
      PropertyGraph::destroy(storage_);
   }
}

void GengoDBGraph::flush() {
   if (!storage_ || dbDir_.empty() || fileName_.empty()) return;
   std::string path = graphPath(dbDir_, fileName_);
   std::FILE* f = std::fopen(path.c_str(), "wb");
   if (!f) throw std::runtime_error("GengoDBGraph::flush: cannot open " + path);

   auto neo = GraphData::serialize(*storage_);
   uint32_t header[4] = {
      GRAPH_FILE_MAGIC,
      static_cast<uint32_t>(neo->nNodes),
      static_cast<uint32_t>(neo->nRels),
      static_cast<uint32_t>(neo->nProps),
   };
   std::fwrite(header, sizeof(header), 1, f);
   std::fwrite(neo->nodes.ptr, sizeof(Neo4JGraph::NodeEntry),   neo->nNodes, f);
   std::fwrite(neo->rels.ptr,  sizeof(Neo4JGraph::RelEntry),    neo->nRels,  f);
   std::fwrite(neo->props.ptr, sizeof(Neo4JGraph::PropRecord),  neo->nProps, f);
   storage_->getPropData().flush(f);
   std::fclose(f);
}

void GengoDBGraph::ensureLoaded() {
   if (loaded_) return;
   loaded_ = true;
   if (storage_->nodeHighWater() > 0) return; // already populated in-memory
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

   Neo4JGraph neo(header[1], header[2], header[3]);
   std::fread(neo.nodes.ptr, sizeof(Neo4JGraph::NodeEntry),  neo.nNodes, f);
   std::fread(neo.rels.ptr,  sizeof(Neo4JGraph::RelEntry),   neo.nRels,  f);
   std::fread(neo.props.ptr, sizeof(Neo4JGraph::PropRecord), neo.nProps, f);

   GraphData::deserialize(*storage_, neo);
   storage_->getPropData().load(f);
   std::fclose(f);
}
bool GengoDBGraph::hasFreshCache(const std::string& sourcePath) const {
   if (dbDir_.empty() || fileName_.empty()) return false;
   std::string path = graphPath(dbDir_, fileName_);
   if (!std::filesystem::exists(path)) return false;
   if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) return true;
   return std::filesystem::last_write_time(path) >= std::filesystem::last_write_time(sourcePath);
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
