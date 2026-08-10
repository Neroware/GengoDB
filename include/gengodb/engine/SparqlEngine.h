#ifndef GENGODB_ENGINE_SPARQLENGINE_H
#define GENGODB_ENGINE_SPARQLENGINE_H

#include "lingodb/execution/Error.h"
#include "lingodb/execution/ResultProcessing.h"
#include "lingodb/runtime/Session.h"

#include "gengodb/semantics/RdfGraph.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gengodb::engine {

using LoadedGraphs = std::vector<std::pair<std::string, const gengodb::semantics::RdfGraph*>>;

// Shared state for a sequence of SPARQL statements executed against one
// database directory - shared verbatim by the `sparql` REPL and the
// `sparql-endpoint` HTTP tool. Mutated in place by handleLoad/handleSettings.
struct EngineState {
   std::shared_ptr<lingodb::runtime::Session> session;
   std::string dbDir;
   LoadedGraphs loadedGraphs;
   std::string defaultGraph;
};

// Buffers statement text fed in chunks, splitting on top-level ';' while
// tracking <IRI>, "string"/'string', and {} nesting depth (SPARQL uses ';'
// inside {} too, as predicate-list shorthand for repeated subjects).
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

// Handles `LOAD <sourceIri> INTO GRAPH <graphIri>;`. Only file:// Turtle (.ttl)
// sources are supported. Throws std::runtime_error on failure.
void handleLoad(EngineState& state, const std::string& sourceIri, const std::string& graphIri);

struct SettingsDirectives {
   bool persists{false};
   bool initialize{false};
   std::optional<std::string> defaultGraph;
};

// Regex-detects the `gengodb://sparql/settings/` escape-prefix directives
// (persists/initialize/defaultGraph) embedded in query-shaped text. This text
// is never actually parsed as SPARQL - it's a REPL-local convention.
std::optional<SettingsDirectives> detectSettingsDirectives(const std::string& stmt);

// Applies directives: initialize rebuilds the GraphNodeIndexCatalogEntry;
// persists persists the catalog and re-creates the session; defaultGraph
// mutates state.defaultGraph (persists as the server-wide default from then
// on). Throws std::runtime_error on failure.
void handleSettings(EngineState& state, const SettingsDirectives& directives);

// Classifies a trimmed statement without executing it, so callers can reject
// e.g. plain queries sent to an update-only endpoint before running them.
enum class StatementKind { Load,
                            Settings,
                            Query };
StatementKind classifyStatement(const std::string& stmt);

// Executes `stmt` as a SPARQL query.
//  - defaultGraphOverride: used instead of state.defaultGraph for this call
//    only; never mutates state. This is how a per-request
//    `default-graph-uri` parameter is threaded through without becoming
//    shared server state.
//  - resultProcessor: if non-null, installed as
//    queryExecutionConfig->resultProcessor (e.g. execution::createTableRetriever
//    to retrieve an arrow::Table programmatically); if null,
//    createQueryExecutionConfig's built-in default (execution::createTablePrinter(),
//    ASCII table to stdout) is used unchanged, matching REPL/run-sparql behavior.
//  - exitOnError: forwarded to QueryExecuter::setExitOnError. Default true
//    preserves the REPL's exact current behavior (process exit(1) on a
//    compile/execution-phase error). Callers that must not crash the process
//    (e.g. an HTTP server) MUST pass false.
//  - throwOnError: if false (default), SPARQL-translation failures are
//    printed to std::cerr as "Error translating SPARQL: <msg>" and the call
//    returns nullptr - byte-identical to today's REPL handleQuery. If true,
//    the same failure (and any execution::Error captured when
//    exitOnError=false) is thrown as std::runtime_error instead, for callers
//    that want to turn it into e.g. an HTTP error response.
// Returns the execution::Error captured before the query task ran (only
// meaningful when exitOnError=false); nullptr if translation failed with
// throwOnError=false, or if the query never got as far as building an
// executer.
std::shared_ptr<lingodb::execution::Error> handleQuery(
   EngineState& state,
   const std::string& stmt,
   std::optional<std::string> defaultGraphOverride = std::nullopt,
   std::unique_ptr<lingodb::execution::ResultProcessor> resultProcessor = nullptr,
   bool exitOnError = true,
   bool throwOnError = false);

// Top-level dispatch: LOAD -> settings-directive -> query, in that order
// (same precedence as the REPL). LOAD/settings failures are printed as
// "Error: <msg>" (throwOnError=false, REPL-identical) or rethrown
// (throwOnError=true). The query case delegates to handleQuery with the same
// parameters.
std::shared_ptr<lingodb::execution::Error> handleStatement(
   EngineState& state,
   const std::string& rawStmt,
   std::optional<std::string> defaultGraphOverride = std::nullopt,
   std::unique_ptr<lingodb::execution::ResultProcessor> resultProcessor = nullptr,
   bool exitOnError = true,
   bool throwOnError = false);

} // namespace gengodb::engine

#endif // GENGODB_ENGINE_SPARQLENGINE_H
