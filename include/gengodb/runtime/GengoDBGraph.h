#ifndef GENGODB_RUNTIME_GENGODBGRAPH_H
#define GENGODB_RUNTIME_GENGODBGRAPH_H

#include "gengodb/runtime/BuiltinGraphs.h"
#include "lingodb/utility/Serialization.h"

#include <memory>
#include <string>

#define DEFAULT_NODE_CAPACITY 1024
#define DEFAULT_REL_CAPACITY 1024
#define DEFAULT_PROP_CAPACITY 1024

namespace lingodb::runtime {

class GengoDBGraph {
public:
   explicit GengoDBGraph(std::string fileName, 
      int32_t nodeCapacity = DEFAULT_NODE_CAPACITY,
      int32_t relCapacity = DEFAULT_REL_CAPACITY,
      int32_t propCapacity = DEFAULT_PROP_CAPACITY) 
         : storage_(PropertyGraph::create(nodeCapacity, relCapacity, propCapacity)),
         fileName_(std::move(fileName)) { }

   virtual ~GengoDBGraph() = default;

   PropertyGraph& storage() { return *storage_; }
   const PropertyGraph& storage() const { return *storage_; }

   void setDBDir(std::string dir) { dbDir_ = std::move(dir); }
   void ensureLoaded();
   void flush();
   bool hasFreshCache(const std::string& sourcePath) const;

   void serialize(lingodb::utility::Serializer& serializer) const;
   static std::unique_ptr<GengoDBGraph> deserialize(lingodb::utility::Deserializer& deserializer);

   static std::unique_ptr<GengoDBGraph> create(std::string name);

private:
   PropertyGraph* storage_;
   std::string fileName_;
   std::string dbDir_;
   bool loaded_ = false;
};

} // namespace lingodb::runtime
#endif // GENGODB_RUNTIME_GENGODBGRAPH_H