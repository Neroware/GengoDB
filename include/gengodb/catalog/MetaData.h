#ifndef GENGODB_CATALOG_METADATA_H
#define GENGODB_CATALOG_METADATA_H

#include "lingodb/utility/HyperLogLog.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lingodb::utility {
class Serializer;
class Deserializer;
} //end namespace lingodb::utility
namespace gengodb::catalog {
class TripleStatistics {
   size_t numTriples = 0;
   std::optional<lingodb::utility::HyperLogLog> subjects;
   std::optional<lingodb::utility::HyperLogLog> objects;

   public:
   TripleStatistics() = default;
   TripleStatistics(size_t numTriples, std::optional<lingodb::utility::HyperLogLog> subjects, std::optional<lingodb::utility::HyperLogLog> objects)
      : numTriples(numTriples), subjects(subjects), objects(objects) {}
   size_t getNumTriples() const { return numTriples; }
   std::optional<size_t> getNumDistinctSubjects() const;
   std::optional<size_t> getNumDistinctObjects() const;
   void add(int32_t subject, int32_t object);
   void merge(const TripleStatistics& other);
   void serialize(lingodb::utility::Serializer& serializer) const;
   static TripleStatistics deserialize(lingodb::utility::Deserializer& deserializer);
};

class GraphMetadataProvider {
   public:
   virtual size_t getNumNodes() const = 0;
   virtual size_t getNumTriples() const = 0;
   virtual std::vector<std::pair<std::string, int32_t>> getPredicates() const = 0;
   virtual const TripleStatistics& getTotalStatistics() const = 0;
   virtual std::optional<int32_t> resolvePredicate(std::string_view iri) const = 0;
   virtual const TripleStatistics* getPredicateStatistics(int32_t predicate) const = 0;
   void serialize(lingodb::utility::Serializer& serializer) const;
   static std::shared_ptr<GraphMetadataProvider> deserialize(lingodb::utility::Deserializer& deserializer);
   virtual ~GraphMetadataProvider() = default;
};

} //end namespace gengodb::catalog

#endif //GENGODB_CATALOG_METADATA_H
