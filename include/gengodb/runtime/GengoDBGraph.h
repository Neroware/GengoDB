#ifndef GENGODB_RUNTIME_GENGODBGRAPH_H
#define GENGODB_RUNTIME_GENGODBGRAPH_H

#include "gengodb/runtime/BuiltinGraphs.h"
#include "lingodb/utility/Serialization.h"

#include <memory>
#include <string>

namespace lingodb::runtime {

class GengoDBGraph {
public:
   explicit GengoDBGraph(std::string fileName)
      : storage_(nullptr),
      fileName_(std::move(fileName)) {}
   explicit GengoDBGraph(std::string fileName, 
      int32_t nodeCapacity, int32_t relCapacity, int32_t propCapacity) 
         : storage_(std::make_unique<PropertyGraph>(nodeCapacity, relCapacity, propCapacity)),
         fileName_(std::move(fileName)) { storage_->registerGraph(); }

   virtual ~GengoDBGraph() { if(storage_) storage_->deregisterGraph(); }

   PropertyGraph& storage() { return *storage_; }
   const PropertyGraph& storage() const { return *storage_; }

   void setDBDir(std::string dir) { dbDir_ = std::move(dir); }
   void ensureLoaded();
   void flush();

   // Catalog serialization: only the file name is persisted; the binary data
   // is stored separately in the db directory.
   void serialize(lingodb::utility::Serializer& serializer) const;
   static std::unique_ptr<GengoDBGraph> deserialize(lingodb::utility::Deserializer& deserializer);

   static std::unique_ptr<GengoDBGraph> create(std::string name);

private:
   std::unique_ptr<PropertyGraph> storage_;
   std::string fileName_;
   std::string dbDir_;
   bool loaded_ = false;
};

} // namespace lingodb::runtime
#endif // GENGODB_RUNTIME_GENGODBGRAPH_H