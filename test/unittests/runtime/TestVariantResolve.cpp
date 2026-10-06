#include "catch2/catch_all.hpp"

#include "gengodb/runtime/Variant.h"
#include "gengodb/semantics/RdfGraph.h"

#include "lingodb/runtime/ExecutionContext.h"
#include "lingodb/runtime/Session.h"
#include <lingodb/scheduler/Tasks.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace gengodb::semantics;
using lingodb::runtime::GraphStorage;
using lingodb::runtime::PropertyGraph;
using lingodb::runtime::VariantRuntime;
using lingodb::runtime::VarLen32;

namespace {
class MockTaskWithContext : public lingodb::scheduler::TaskWithContext {
   std::function<void()> job;

   public:
   MockTaskWithContext(lingodb::runtime::ExecutionContext* context, std::function<void()> job) : TaskWithContext(context), job(std::move(job)) {}
   bool allocateWork() override {
      if (workExhausted.exchange(true)) {
         return false;
      }
      return true;
   }
   void performWork() override {
      job();
   }
};
template <class F>
void withContext(const F& f) {
   auto scheduler = lingodb::scheduler::startScheduler();
   auto session = lingodb::runtime::Session::createSession();
   lingodb::runtime::ExecutionContext context(*session);
   lingodb::scheduler::awaitEntryTask(std::make_unique<MockTaskWithContext>(&context, [&]() {
      f();
   }));
}

const std::string kEx = "http://example.org/";
const std::string kXsd = "http://www.w3.org/2001/XMLSchema#";
IRI ex(const std::string& name) { return IRI{kEx + name}; }
Literal typed(const std::string& lex, const std::string& datatype) { return Literal::make_typed(lex, IRI{kXsd + datatype}); }

// Literals of every storage tier (inline, fixed 8 byte, blob, graph-node backed). Values shared by
// two subjects are separated by other values of the same tier, so that the property storage grows
// in between (literal nodes must still be deduplicated).
std::vector<std::tuple<IRI, IRI, Literal>> literalTriples() {
   return {
      {ex("bob"), ex("age"), typed("30", "int")},
      {ex("bob"), ex("awake"), typed("true", "boolean")},
      {ex("bob"), ex("offset"), typed("-5", "byte")},
      {ex("bob"), ex("queue"), typed("300", "short")},
      {ex("bob"), ex("temperature"), typed("21.5", "float")},
      {ex("bob"), ex("table"), typed("12", "unsignedByte")},
      {ex("bob"), ex("visits"), typed("1200", "unsignedInt")},
      {ex("bob"), ex("tier"), typed("3", "unsignedShort")},
      {ex("bob"), ex("rating"), typed("4.75", "double")},
      {ex("bob"), ex("balance"), typed("-987654321012345", "long")},
      {ex("bob"), ex("points"), typed("18446744073709551615", "unsignedLong")},
      {ex("bob"), ex("birthday"), typed("1999-11-04", "date")},
      {ex("bob"), ex("lastVisit"), typed("2024-01-02T03:04:05Z", "dateTime")},
      {ex("bob"), ex("name"), typed("Bob", "string")},
      {ex("bob"), ex("motto"), typed("a string that is longer than twelve bytes", "string")},
      {ex("bob"), ex("customerId"), typed("123456789012345678901234567890", "integer")},
      {ex("bob"), ex("share"), typed("0.25", "decimal")},
      {ex("bob"), ex("nickname"), Literal::make_lang_tagged("Bobby", "en")},
      {ex("bob"), ex("since"), typed("1999", "gYear")},
      {ex("bob"), ex("price"), Literal::make_typed("2703.12", ex("USD"))},
      {ex("coffee"), ex("price"), typed("3.5", "double")},
      {ex("coffee"), ex("ratio"), typed("3.14159", "double")},
      {ex("coffee"), ex("batch"), typed("987654321098765", "long")},
      {ex("coffee"), ex("temp"), typed("65", "int")},
      {ex("steve"), ex("rating"), typed("4.75", "double")},
      {ex("steve"), ex("balance"), typed("-987654321012345", "long")},
      {ex("steve"), ex("name"), typed("Bob", "string")},
      {ex("steve"), ex("age"), typed("30", "int")},
      {ex("steve"), ex("nickname"), Literal::make_lang_tagged("Bobby", "en")},
   };
}

// Registers the graph storage for the runtime (GraphStorage::graphPtr) for the scope of a test.
struct TestGraph {
   std::unique_ptr<RdfGraph> rdf;
   explicit TestGraph(const std::string& name, bool reversed) : rdf(RdfGraph::create(name, IRI{kEx + name})) {
      auto triples = literalTriples();
      if (reversed) std::reverse(triples.begin(), triples.end());
      for (auto& [s, p, o] : triples) {
         rdf->addTriple(s, p, o);
      }
      rdf->addTriple(ex("bob"), ex("drinks"), ex("coffee"));
      rdf->addTriple(ex("bob"), ex("cup"), BlankNode{"cup1"});
      rdf->ensureLoaded();
      storage().setPersists(true);
      storage().registerGraph();
   }
   ~TestGraph() { GraphStorage::remove(reinterpret_cast<const uint8_t*>(&storage())); }
   PropertyGraph& storage() { return rdf->getStorage().storage(); }
   uint8_t* nodeSet() { return storage().nodeStorePtr(); }
   int32_t size() { return static_cast<int32_t>(rdf->getNodes().size()); }
   RDFNodeType type(int32_t id) { return rdf->getNodes().get(id).type; }
   std::string term(int32_t id) { return storage().getMetadata().get_node_name(id); }
};

bool isNumericFamily(xsd::Type t) {
   switch (t) {
      case xsd::Type::Boolean: case xsd::Type::Long: case xsd::Type::Double: case xsd::Type::Byte:
      case xsd::Type::Short: case xsd::Type::Int: case xsd::Type::UnsignedByte: case xsd::Type::UnsignedShort:
      case xsd::Type::UnsignedInt: case xsd::Type::UnsignedLong: case xsd::Type::Float: case xsd::Type::Date:
      case xsd::Type::DateTime:
         return true;
      default:
         return false;
   }
}

// The variant (tag, payload) the lowering of variant.create_node_ref builds for a graph node.
struct BoundVariant {
   int32_t tag;
   int64_t payload;
};
BoundVariant variantOf(TestGraph& graph, int32_t id, std::vector<std::unique_ptr<VarLen32>>& scratch) {
   auto* ref = &graph.storage().node(id);
   const int32_t tag = VariantRuntime::resolveRefTag(ref);
   auto t = xsd::from_int32(tag);
   if (t && isNumericFamily(*t)) {
      uint8_t bytes[8];
      std::memset(bytes, 0xab, sizeof(bytes)); // the lowering's stack slot is uninitialized
      VariantRuntime::extractNumericLiteral(ref, tag, bytes);
      int64_t payload;
      std::memcpy(&payload, bytes, sizeof(payload));
      return {tag, payload};
   }
   if (t && (*t == xsd::Type::String || *t == xsd::Type::Integer || *t == xsd::Type::Decimal)) {
      scratch.push_back(std::make_unique<VarLen32>(VariantRuntime::extractBlobLiteral(ref)));
      return {tag, reinterpret_cast<int64_t>(scratch.back().get())};
   }
   return {tag, reinterpret_cast<int64_t>(ref)};
}
int32_t resolve(TestGraph& target, BoundVariant v) {
   return VariantRuntime::resolveLocalNode(target.nodeSet(), v.tag, v.payload);
}
} // namespace

TEST_CASE("VariantRuntime:resolveLocalNode:SameGraph") {
   withContext([]() {
      TestGraph g("resolve_same", false);
      std::vector<std::unique_ptr<VarLen32>> scratch;
      for (int32_t id = 0; id < g.size(); id++) {
         INFO("node " << id << " " << g.term(id));
         REQUIRE(resolve(g, variantOf(g, id, scratch)) == id);
      }
   });
}

TEST_CASE("VariantRuntime:resolveLocalNode:LiteralNodesAreDeduplicated") {
   withContext([]() {
      TestGraph g("resolve_dedup", false);
      std::unordered_map<std::string, int32_t> seen;
      for (int32_t id = 0; id < g.size(); id++) {
         if (g.type(id) != RDFNodeType::Literal) continue;
         INFO("literal " << g.term(id));
         REQUIRE(seen.emplace(g.term(id), id).second);
      }
      REQUIRE(seen.size() == 24);
   });
}

TEST_CASE("VariantRuntime:resolveLocalNode:OtherGraph") {
   withContext([]() {
      TestGraph source("resolve_source", false);
      TestGraph target("resolve_target", true);
      std::vector<std::unique_ptr<VarLen32>> scratch;
      for (int32_t id = 0; id < source.size(); id++) {
         INFO("node " << id << " " << source.term(id));
         auto v = variantOf(source, id, scratch);
         if (source.type(id) == RDFNodeType::BNode) {
            // blank nodes can only be matched across graphs through the global node index
            REQUIRE_THROWS_AS(resolve(target, v), std::runtime_error);
            continue;
         }
         const int32_t targetId = resolve(target, v);
         REQUIRE(targetId >= 0);
         REQUIRE(target.term(targetId) == source.term(id));
      }
   });
}

TEST_CASE("VariantRuntime:resolveLocalNode:ComputedValues") {
   withContext([]() {
      TestGraph g("resolve_computed", false);
      auto idOf = [&](const std::string& term) {
         for (int32_t id = 0; id < g.size(); id++) {
            if (g.term(id) == term) return id;
         }
         return -1;
      };
      auto str = [](const std::string& s) { return std::make_unique<VarLen32>(VarLen32::fromString(s)); };
      auto bob = str("Bob");
      auto bobIri = str(kEx + "bob");
      auto nobody = str("Nobody");
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::String), reinterpret_cast<int64_t>(bob.get())) == idOf("\"Bob\""));
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::AnyIRI), reinterpret_cast<int64_t>(bobIri.get())) == idOf("<" + kEx + "bob>"));
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::Int), 30) == idOf("\"30\"^^<" + kXsd + "int>"));
      double rating = 4.75;
      int64_t ratingBits;
      std::memcpy(&ratingBits, &rating, sizeof(rating));
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::Double), ratingBits) >= 0);
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::String), reinterpret_cast<int64_t>(nobody.get())) == -1);
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::Int), 31) == -1);
      REQUIRE(VariantRuntime::resolveLocalNode(g.nodeSet(), xsd::to_int32(xsd::Type::Unspecified), 0) == -1);
   });
}
