#include "gengodb/catalog/MetaData.h"

#include "lingodb/utility/Serialization.h"

#include <unordered_map>

namespace gengodb::catalog {
namespace {
// HyperLogLog expects uniformly distributed 64-bit hashes
uint64_t hashNodeId(int32_t id) {
   uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(id));
   h ^= h >> 33;
   h *= 0xff51afd7ed558ccdull;
   h ^= h >> 33;
   h *= 0xc4ceb9fe1a85ec53ull;
   h ^= h >> 33;
   return h;
}
std::optional<size_t> estimate(const std::optional<lingodb::utility::HyperLogLog>& hll) {
   if (!hll.has_value()) return std::nullopt;
   auto copy = hll.value();
   return static_cast<size_t>(copy.estimate());
}
void mergeHll(std::optional<lingodb::utility::HyperLogLog>& into, const std::optional<lingodb::utility::HyperLogLog>& other) {
   if (!other.has_value()) return;
   if (!into.has_value()) {
      into = other;
   } else {
      into->merge(other.value());
   }
}
} // namespace

std::optional<size_t> TripleStatistics::getNumDistinctSubjects() const {
   return estimate(subjects);
}
std::optional<size_t> TripleStatistics::getNumDistinctObjects() const {
   return estimate(objects);
}
void TripleStatistics::add(int32_t subject, int32_t object) {
   if (!subjects.has_value()) subjects.emplace();
   if (!objects.has_value()) objects.emplace();
   subjects->add(hashNodeId(subject));
   objects->add(hashNodeId(object));
   numTriples++;
}
void TripleStatistics::merge(const TripleStatistics& other) {
   numTriples += other.numTriples;
   mergeHll(subjects, other.subjects);
   mergeHll(objects, other.objects);
}
void TripleStatistics::serialize(lingodb::utility::Serializer& serializer) const {
   serializer.writeProperty(1, numTriples);
   serializer.writeProperty(2, subjects);
   serializer.writeProperty(3, objects);
}
TripleStatistics TripleStatistics::deserialize(lingodb::utility::Deserializer& deserializer) {
   auto numTriples = deserializer.readProperty<size_t>(1);
   auto subjects = deserializer.readProperty<std::optional<lingodb::utility::HyperLogLog>>(2);
   auto objects = deserializer.readProperty<std::optional<lingodb::utility::HyperLogLog>>(3);
   return {numTriples, subjects, objects};
}

class StoredGraphMetadata : public GraphMetadataProvider {
   size_t numNodes = 0;
   TripleStatistics total;
   std::unordered_map<int32_t, TripleStatistics> perPredicate;
   std::unordered_map<std::string, int32_t> predicateIds;

   public:
   StoredGraphMetadata() = default;
   StoredGraphMetadata(size_t numNodes, TripleStatistics total, std::unordered_map<int32_t, TripleStatistics> perPredicate, std::unordered_map<std::string, int32_t> predicateIds)
      : numNodes(numNodes), total(std::move(total)), perPredicate(std::move(perPredicate)), predicateIds(std::move(predicateIds)) {}
   size_t getNumNodes() const override { return numNodes; }
   size_t getNumTriples() const override { return total.getNumTriples(); }
   const TripleStatistics& getTotalStatistics() const override { return total; }
   std::optional<int32_t> resolvePredicate(std::string_view iri) const override;
   const TripleStatistics* getPredicateStatistics(int32_t predicate) const override;
   std::vector<std::pair<std::string, int32_t>> getPredicates() const override;
   void addTriple(int32_t subject, int32_t predicate, int32_t object);
   void addPredicateIri(std::string iri, int32_t predicate);
   void setNumNodes(size_t numNodes) { this->numNodes = numNodes; }
};
std::optional<int32_t> StoredGraphMetadata::resolvePredicate(std::string_view iri) const {
   auto it = predicateIds.find(std::string(iri));
   if (it == predicateIds.end()) return std::nullopt;
   return it->second;
}
const TripleStatistics* StoredGraphMetadata::getPredicateStatistics(int32_t predicate) const {
   auto it = perPredicate.find(predicate);
   if (it == perPredicate.end()) return nullptr;
   return &it->second;
}
std::vector<std::pair<std::string, int32_t>> StoredGraphMetadata::getPredicates() const {
   return {predicateIds.begin(), predicateIds.end()};
}
void StoredGraphMetadata::addTriple(int32_t subject, int32_t predicate, int32_t object) {
   total.add(subject, object);
   perPredicate[predicate].add(subject, object);
}
void StoredGraphMetadata::addPredicateIri(std::string iri, int32_t predicate) {
   predicateIds.insert_or_assign(std::move(iri), predicate);
}

void GraphMetadataProvider::serialize(lingodb::utility::Serializer& serializer) const {
   auto predicates = getPredicates();
   serializer.writeProperty(1, getNumNodes());
   serializer.writeProperty(2, getTotalStatistics());
   serializer.writeProperty(3, predicates.size());
   for (const auto& [iri, id] : predicates) {
      serializer.writeProperty(4, iri);
      serializer.writeProperty(5, id);
      const auto* stats = getPredicateStatistics(id);
      serializer.writeProperty(6, stats ? *stats : TripleStatistics{});
   }
}
std::shared_ptr<GraphMetadataProvider> GraphMetadataProvider::deserialize(lingodb::utility::Deserializer& deserializer) {
   auto numNodes = deserializer.readProperty<size_t>(1);
   auto total = deserializer.readProperty<TripleStatistics>(2);
   auto numPredicates = deserializer.readProperty<size_t>(3);
   std::unordered_map<int32_t, TripleStatistics> perPredicate;
   std::unordered_map<std::string, int32_t> predicateIds;
   for (size_t i = 0; i < numPredicates; i++) {
      auto iri = deserializer.readProperty<std::string>(4);
      auto id = deserializer.readProperty<int32_t>(5);
      auto stats = deserializer.readProperty<TripleStatistics>(6);
      if (stats.getNumTriples() > 0) {
         perPredicate.emplace(id, std::move(stats));
      }
      predicateIds.emplace(std::move(iri), id);
   }
   return std::make_shared<StoredGraphMetadata>(numNodes, std::move(total), std::move(perPredicate), std::move(predicateIds));
}

} // namespace gengodb::catalog
