#include "features.h"
#include "linenoise.h"

#include "lingodb/compiler/mlir-support/eval.h"
#include "lingodb/execution/Execution.h"
#include "lingodb/scheduler/Scheduler.h"

#include "gengodb/compiler/frontend/SparqlMLIRTranslator.h"

#include "gengodb/catalog/CreateRdfGraphDef.h"
#include "gengodb/catalog/GraphCatalogEntry.h"
#include "gengodb/catalog/GraphNodeIndexCatalogEntry.h"
#include "gengodb/semantics/RdfFileFormat.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

using namespace lingodb;
using namespace gengodb::catalog;
using namespace gengodb::semantics;

namespace {

namespace fs = std::filesystem;

using LoadedGraphs = std::vector<std::pair<std::string, const RdfGraph*>>;

struct ToolState {
   std::shared_ptr<runtime::Session> session;
   std::string dbDir;
   LoadedGraphs loadedGraphs;
   std::string defaultGraph;
};

class StatementAccumulator {
   std::string buf;
   int depth{0};
   bool inIri{false};
   bool inStr{false};
   char strDelim{0};

   public:
   std::vector<std::string> feed(const std::string& chunk) {
      std::vector<std::string> out;
      for (size_t i = 0; i < chunk.size(); i++) {
         char c = chunk[i];
         if (inIri) {
            buf += c;
            if (c == '>') inIri = false;
            continue;
         }
         if (inStr) {
            buf += c;
            if (c == '\\' && i + 1 < chunk.size()) { buf += chunk[++i]; continue; }
            if (c == strDelim) inStr = false;
            continue;
         }
         if (c == '#') {
            while (i < chunk.size() && chunk[i] != '\n') i++;
            if (i < chunk.size()) buf += '\n';
            continue;
         }
         if (c == '<') { inIri = true; buf += c; continue; }
         if (c == '"' || c == '\'') { inStr = true; strDelim = c; buf += c; continue; }
         if (c == '{') { depth++; buf += c; continue; }
         if (c == '}') { depth--; buf += c; continue; }
         if (c == ';' && depth <= 0) {
            out.push_back(buf);
            buf.clear();
            continue;
         }
         buf += c;
      }
      return out;
   }
   bool hasPending() const {
      return buf.find_first_not_of(" \t\r\n") != std::string::npos;
   }
   bool atTopLevel() const { return depth <= 0 && !inIri && !inStr; }
};

std::string trim(const std::string& s) {
   auto b = s.find_first_not_of(" \t\r\n");
   if (b == std::string::npos) return "";
   auto e = s.find_last_not_of(" \t\r\n");
   return s.substr(b, e - b + 1);
}

const std::regex& loadStatementRegex() {
   static const std::regex re(R"(^LOAD\s*<([^>]+)>\s*INTO\s+GRAPH\s*<([^>]+)>$)", std::regex::icase);
   return re;
}

void handleLoad(ToolState& state, const std::string& sourceIri, const std::string& graphIri) {
   // Only Turtle sources are supported for now.
   static const std::string ttlExt = ".ttl";
   if (sourceIri.size() < ttlExt.size() || sourceIri.compare(sourceIri.size() - ttlExt.size(), ttlExt.size(), ttlExt) != 0)
      throw std::runtime_error("LOAD: only Turtle (.ttl) sources are supported for now: <" + sourceIri + ">");
   if (sourceIri.rfind("file://", 0) != 0)
      throw std::runtime_error("LOAD: only file:// source IRIs are supported for now: <" + sourceIri + ">");

   std::string name = uriAlias(graphIri);
   std::string filename = uriAlias(sourceIri);

   std::string expectedPath = state.dbDir + filename + ttlExt;
   if (!fs::exists(expectedPath))
      throw std::runtime_error("LOAD: expected RDF file at '" + expectedPath + "' (derived from GRAPH <" + sourceIri + ">) but it was not found");

   auto& catalog = *state.session->getCatalog();
   if (auto existing = catalog.getTypedEntry<RDFGraphCatalogEntry>(name)) {
      auto entry = existing.value();
      if (entry->getFormat() != RDFFileFormat::BINARY) entry->ensureFullyLoaded();
      state.loadedGraphs.push_back({entry->getName(), &entry->getGraph()});
      std::cout << "Graph <" << graphIri << "> already loaded as '" << name << "'." << std::endl;
      return;
   }

   CreateRdfGraphDef def{name, rdf4cpp::IRI{graphIri}, RDFFileFormat::TURTLE, filename};
   auto graphEntry = RDFGraphCatalogEntry::createFromCreateRdfGraphDef(def);
   catalog.insertEntry(graphEntry);
   graphEntry->ensureFullyLoaded();
   state.loadedGraphs.push_back({graphEntry->getName(), &graphEntry->getGraph()});
   std::cout << "Loaded <" << sourceIri << "> into graph '" << name << "' (<" << graphIri << ">)." << std::endl;
}

const std::string& settingsNamespace() {
   static const std::string ns = "gengodb://sparql/settings/";
   return ns;
}

std::map<std::string, std::string> extractPrefixes(const std::string& stmt) {
   static const std::regex re(R"(PREFIX\s+([A-Za-z_][\w-]*)?\s*:\s*<([^>]*)>)", std::regex::icase);
   std::map<std::string, std::string> prefixes;
   for (auto it = std::sregex_iterator(stmt.begin(), stmt.end(), re); it != std::sregex_iterator(); ++it)
      prefixes[(*it)[1].str()] = (*it)[2].str();
   return prefixes;
}

bool hasSettingsDirective(const std::string& stmt, const std::string& localName, const std::optional<std::string>& label) {
   std::string pattern = "<" + settingsNamespace() + localName + ">\\s+true\\b";
   if (label) pattern += "|\\b" + *label + ":" + localName + "\\s+true\\b";
   std::regex re(pattern, std::regex::icase);
   return std::regex_search(stmt, re);
}

std::optional<std::string> settingsDirectiveIri(const std::string& stmt, const std::string& localName, const std::optional<std::string>& label) {
   std::string pattern = "<" + settingsNamespace() + localName + ">\\s*<([^>]+)>";
   if (label) pattern += "|\\b" + *label + ":" + localName + "\\s*<([^>]+)>";
   std::regex re(pattern, std::regex::icase);
   std::smatch m;
   if (!std::regex_search(stmt, m, re)) return std::nullopt;
   return m[1].matched ? m[1].str() : m[2].str();
}

struct SettingsDirectives {
   bool persists{false};
   bool initialize{false};
   std::optional<std::string> defaultGraph;
};

std::optional<SettingsDirectives> detectSettingsDirectives(const std::string& stmt) {
   std::optional<std::string> label;
   for (auto& [l, iri] : extractPrefixes(stmt)) {
      if (iri == settingsNamespace()) { label = l; break; }
   }
   SettingsDirectives directives{
      hasSettingsDirective(stmt, "persists", label),
      hasSettingsDirective(stmt, "initialize", label),
      settingsDirectiveIri(stmt, "defaultGraph", label)};
   if (!directives.persists && !directives.initialize && !directives.defaultGraph) return std::nullopt;
   return directives;
}

void handleSettings(ToolState& state, const SettingsDirectives& directives) {
   if (directives.initialize) {
      auto indexEntry = GraphNodeIndexCatalogEntry::build(state.loadedGraphs);
      state.session->getCatalog()->insertEntry(indexEntry, /*replace=*/true);
      std::cout << "Rebuilt graph node index over " << state.loadedGraphs.size() << " graph(s)." << std::endl;
   }
   if (directives.persists) {
      state.session->getCatalog()->setShouldPersist(true);
      state.session->getCatalog()->persist();
      std::cout << "Persisted catalog to disk." << std::endl;
      std::vector<std::string> names;
      for (auto& [name, graph] : state.loadedGraphs) names.push_back(name);
      state.session = runtime::Session::createSession(state.dbDir, /*eagerLoading=*/true);
      state.loadedGraphs.clear();
      for (auto& name : names) {
         if (auto entry = state.session->getCatalog()->getTypedEntry<RDFGraphCatalogEntry>(name))
            state.loadedGraphs.push_back({name, &entry.value()->getGraph()});
      }
   }
   if (directives.defaultGraph) {
      state.defaultGraph = *directives.defaultGraph;
      std::cout << "Default graph set to <" << state.defaultGraph << ">." << std::endl;
   }
}

void handleQuery(ToolState& state, const std::string& stmt) {
   std::string mlirText;
   try {
      mlirText = translateSparqlToMLIRString(stmt, state.defaultGraph);
   } catch (const std::exception& e) {
      std::cerr << "Error translating SPARQL: " << e.what() << std::endl;
      return;
   }
   auto queryExecutionConfig = execution::createQueryExecutionConfig(execution::getExecutionMode(), false);
   auto executer = execution::QueryExecuter::createDefaultExecuter(std::move(queryExecutionConfig), *state.session);
   executer->fromData(mlirText);
   scheduler::awaitEntryTask(std::make_unique<execution::QueryExecutionTask>(std::move(executer)));
}

void handleStatement(ToolState& state, const std::string& rawStmt) {
   std::string stmt = trim(rawStmt);
   if (stmt.empty()) return;

   std::smatch m;
   if (std::regex_match(stmt, m, loadStatementRegex())) {
      try {
         handleLoad(state, m[1].str(), m[2].str());
      } catch (const std::exception& e) {
         std::cerr << "Error: " << e.what() << std::endl;
      }
      return;
   }

   if (auto directives = detectSettingsDirectives(stmt)) {
      try {
         handleSettings(state, *directives);
      } catch (const std::exception& e) {
         std::cerr << "Error: " << e.what() << std::endl;
      }
      return;
   }

   handleQuery(state, stmt);
}

} // namespace

int main(int argc, char** argv) {
   if (argc == 2 && std::string(argv[1]) == "--features") {
      printFeatures();
      return 0;
   }
   if (argc <= 1) {
      std::cerr << "USAGE: sparql database" << std::endl;
      return 1;
   }
   ToolState state;
   state.dbDir = argv[1];
   state.session = runtime::Session::createSession(state.dbDir, true);

   compiler::support::eval::init();
   auto scheduler = scheduler::startScheduler();

   linenoiseSetMultiLine(true);
   StatementAccumulator acc;
   while (true) {
      const char* promptStr = acc.atTopLevel() ? "sparql> " : "     -> ";
      char* input = linenoise(promptStr);
      if (input == nullptr) {
         // Ctrl+D or EOF
         std::cout << std::endl;
         break;
      }
      std::string line = input;
      free(input);

      if (acc.atTopLevel() && trim(line) == "exit") break;

      auto statements = acc.feed(line + "\n");
      for (auto& stmt : statements) {
         linenoiseHistoryAdd(stmt.c_str());
         handleStatement(state, stmt);
      }
   }
   if (acc.hasPending()) std::cerr << "Warning: trailing input not terminated by ';', ignoring it." << std::endl;
   linenoiseHistoryFree();
   return 0;
}
