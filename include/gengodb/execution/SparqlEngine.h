#ifndef GENGODB_EXECUTION_SPARQLENGINE_H
#define GENGODB_EXECUTION_SPARQLENGINE_H

#include "lingodb/execution/Error.h"
#include "lingodb/execution/ResultProcessing.h"
#include "lingodb/runtime/Session.h"

#include "gengodb/runtime/GengoDBGraph.h"
#include "gengodb/semantics/RdfGraph.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gengodb::execution {

using LoadedGraphs = std::vector<std::pair<std::string, const gengodb::semantics::RdfGraph*>>;

struct EngineState {
   std::shared_ptr<lingodb::runtime::Session> session;
   std::string dbDir;
   LoadedGraphs loadedGraphs;
   std::string defaultGraph = "gengodb://sparql/settings/defaultGraph#rdf";
   int32_t graphCapacity = DEFAULT_NODE_CAPACITY;
};

class StatementAccumulator {
   public:
   std::vector<std::string> feed(const std::string& chunk);
   bool hasPending() const;
   bool atTopLevel() const { return depth <= 0 && !inIri && !inStr; }

   private:
   std::string buf;
   int depth{0};
   bool inIri{false};
   bool inStr{false};
   char strDelim{0};
};

std::string trim(const std::string& s);

void handleLoad(EngineState& state, const std::string& sourceIri, const std::string& graphIri);

struct SettingsDirectives {
   bool persists{false};
   bool initialize{false};
   std::optional<std::string> defaultGraph;
   std::optional<int32_t> capacity;
};

std::optional<SettingsDirectives> detectSettingsDirectives(const std::string& stmt);

void handleSettings(EngineState& state, const SettingsDirectives& directives);

enum class StatementKind { Load,
                            Settings,
                            Query };
StatementKind classifyStatement(const std::string& stmt);

std::shared_ptr<lingodb::execution::Error> handleQuery(
   EngineState& state,
   const std::string& stmt,
   std::optional<std::string> defaultGraphOverride = std::nullopt,
   std::unique_ptr<lingodb::execution::ResultProcessor> resultProcessor = nullptr,
   bool exitOnError = true,
   bool throwOnError = false);

std::shared_ptr<lingodb::execution::Error> handleStatement(
   EngineState& state,
   const std::string& rawStmt,
   std::optional<std::string> defaultGraphOverride = std::nullopt,
   std::unique_ptr<lingodb::execution::ResultProcessor> resultProcessor = nullptr,
   bool exitOnError = true,
   bool throwOnError = false);

} // namespace gengodb::execution

#endif // GENGODB_EXECUTION_SPARQLENGINE_H
