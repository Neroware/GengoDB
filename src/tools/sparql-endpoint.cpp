// SPARQL 1.1 Protocol HTTP endpoint for GengoDB.
//
// Exposes exactly two routes at the server root, mirroring the `sparql` REPL's
// engine (see gengodb::engine::handleStatement) over HTTP:
//   POST /sparql-update  - LOAD / settings-directive statements (see below)
//   GET|POST /sparql     - SPARQL SELECT queries
//
// Known, permanent limitation: the compiler pipeline (StringifyVariants pass)
// stringifies every bound RDF term before it reaches the result table, but the
// stringified form does preserve enough syntax to recover term *kind*: IRIs
// are rendered as "<iri>" and blank nodes as "_:label" (see
// classifyTerm() below), so the SPARQL JSON writer uses that convention to set
// "type": "uri"/"bnode"/"literal". Datatype IRIs and language tags are NOT
// preserved anywhere in the pipeline and cannot be recovered here - the JSON
// output is best-effort, not spec-complete.

#include "features.h"

#include "lingodb/compiler/mlir-support/eval.h"
#include "lingodb/execution/ResultProcessing.h"
#include "lingodb/scheduler/Scheduler.h"

#include "gengodb/engine/SparqlEngine.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <arrow/array.h>
#include <arrow/table.h>

#include "json.h"

#include <cctype>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

using namespace lingodb;
using namespace gengodb::engine;

namespace {

std::string urlDecode(std::string_view in) {
   std::string out;
   out.reserve(in.size());
   for (size_t i = 0; i < in.size(); i++) {
      char c = in[i];
      if (c == '+') {
         out += ' ';
      } else if (c == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
         std::string hex(in.substr(i + 1, 2));
         out += static_cast<char>(std::stoi(hex, nullptr, 16));
         i += 2;
      } else {
         out += c;
      }
   }
   return out;
}

std::map<std::string, std::string> parseFormEncoded(const std::string& body) {
   std::map<std::string, std::string> result;
   size_t pos = 0;
   while (pos <= body.size()) {
      size_t amp = body.find('&', pos);
      std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      if (!pair.empty()) {
         auto eq = pair.find('=');
         std::string key = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
         std::string value = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
         if (!key.empty()) result[key] = value;
      }
      if (amp == std::string::npos) break;
      pos = amp + 1;
   }
   return result;
}

class EngineFacade {
   public:
   explicit EngineFacade(EngineState state) : state_(std::move(state)) {}

   template <class F>
   auto withLock(F&& f) -> decltype(f(std::declval<EngineState&>())) {
      std::lock_guard<std::mutex> lock(mutex_);
      return f(state_);
   }

   private:
   std::mutex mutex_;
   EngineState state_;
};

struct TableRows {
   std::vector<std::string> vars;
   std::vector<std::vector<std::optional<std::string>>> rows;
};

std::optional<std::string> arrowStringAt(const std::shared_ptr<arrow::Array>& chunk, int64_t idx) {
   if (chunk->IsNull(idx)) return std::nullopt;
   if (auto* strArr = dynamic_cast<arrow::StringArray*>(chunk.get())) return std::string(strArr->GetView(idx));
   if (auto* largeArr = dynamic_cast<arrow::LargeStringArray*>(chunk.get())) return std::string(largeArr->GetView(idx));
   return std::nullopt;
}

TableRows extractRows(const std::shared_ptr<arrow::Table>& table) {
   TableRows out;
   if (!table) return out;
   for (auto& f : table->schema()->fields()) out.vars.push_back(f->name());
   int numCols = table->num_columns();
   if (numCols == 0) return out;

   std::vector<std::shared_ptr<arrow::ChunkedArray>> cols;
   for (int c = 0; c < numCols; c++) cols.push_back(table->column(c));
   std::vector<int> chunkIdx(numCols, 0), withinChunkIdx(numCols, 0);

   int64_t numRows = table->num_rows();
   for (int64_t row = 0; row < numRows; row++) {
      std::vector<std::optional<std::string>> rowVals;
      rowVals.reserve(numCols);
      for (int c = 0; c < numCols; c++) {
         auto& chunks = cols[c]->chunks();
         while (chunkIdx[c] < static_cast<int>(chunks.size()) && withinChunkIdx[c] >= chunks[chunkIdx[c]]->length()) {
            chunkIdx[c]++;
            withinChunkIdx[c] = 0;
         }
         if (chunkIdx[c] >= static_cast<int>(chunks.size())) {
            rowVals.push_back(std::nullopt);
            continue;
         }
         rowVals.push_back(arrowStringAt(chunks[chunkIdx[c]], withinChunkIdx[c]));
         withinChunkIdx[c]++;
      }
      out.rows.push_back(std::move(rowVals));
   }
   return out;
}

enum class ResultFormat { Csv,
                           Tsv,
                           SparqlJson };

ResultFormat negotiateFormat(const std::string& accept) {
   if (accept.find("text/csv") != std::string::npos) return ResultFormat::Csv;
   if (accept.find("text/tab-separated-values") != std::string::npos) return ResultFormat::Tsv;
   if (accept.find("application/sparql-results+json") != std::string::npos) return ResultFormat::SparqlJson;
   return ResultFormat::SparqlJson; // default (SPARQL's own native result format)
}

std::string contentTypeFor(ResultFormat fmt) {
   switch (fmt) {
      case ResultFormat::Csv: return "text/csv; charset=utf-8";
      case ResultFormat::Tsv: return "text/tab-separated-values; charset=utf-8";
      case ResultFormat::SparqlJson: return "application/sparql-results+json; charset=utf-8";
   }
   return "application/sparql-results+json; charset=utf-8";
}

std::string csvEscape(const std::string& v) {
   if (v.find_first_of(",\"\n\r") == std::string::npos) return v;
   std::string out = "\"";
   for (char c : v) {
      if (c == '"') out += "\"\"";
      else out += c;
   }
   out += "\"";
   return out;
}

std::string toCsv(const TableRows& t) {
   std::ostringstream os;
   for (size_t i = 0; i < t.vars.size(); i++) {
      if (i) os << ",";
      os << csvEscape(t.vars[i]);
   }
   os << "\r\n";
   for (auto& row : t.rows) {
      for (size_t i = 0; i < row.size(); i++) {
         if (i) os << ",";
         if (row[i]) os << csvEscape(*row[i]);
      }
      os << "\r\n";
   }
   return os.str();
}

std::string tsvEscape(const std::string& v) {
   std::string out;
   out.reserve(v.size());
   for (char c : v) {
      if (c == '\t') out += "\\t";
      else if (c == '\n') out += "\\n";
      else if (c == '\r') continue;
      else out += c;
   }
   return out;
}

std::string toTsv(const TableRows& t) {
   std::ostringstream os;
   for (size_t i = 0; i < t.vars.size(); i++) {
      if (i) os << "\t";
      os << "?" << t.vars[i];
   }
   os << "\n";
   for (auto& row : t.rows) {
      for (size_t i = 0; i < row.size(); i++) {
         if (i) os << "\t";
         if (row[i]) os << tsvEscape(*row[i]);
      }
      os << "\n";
   }
   return os.str();
}

struct TermInfo {
   std::string type;
   std::string value;
};

TermInfo classifyTerm(const std::string& lexical) {
   if (lexical.size() >= 2 && lexical.front() == '<' && lexical.back() == '>')
      return {"uri", lexical.substr(1, lexical.size() - 2)};
   if (lexical.rfind("_:", 0) == 0)
      return {"bnode", lexical.substr(2)};
   return {"literal", lexical};
}

std::string toSparqlJson(const TableRows& t) {
   nlohmann::json j;
   j["head"]["vars"] = t.vars;
   j["results"]["bindings"] = nlohmann::json::array();
   for (auto& row : t.rows) {
      nlohmann::json binding = nlohmann::json::object();
      for (size_t i = 0; i < t.vars.size() && i < row.size(); i++) {
         if (!row[i]) continue; // unbound variable: omitted, per the SPARQL Results JSON spec
         auto term = classifyTerm(*row[i]);
         nlohmann::json b;
         b["type"] = term.type;
         b["value"] = term.value;
         binding[t.vars[i]] = std::move(b);
      }
      j["results"]["bindings"].push_back(std::move(binding));
   }
   return j.dump();
}

std::string serialize(ResultFormat fmt, const std::shared_ptr<arrow::Table>& table) {
   TableRows rows = extractRows(table);
   switch (fmt) {
      case ResultFormat::Csv: return toCsv(rows);
      case ResultFormat::Tsv: return toTsv(rows);
      case ResultFormat::SparqlJson: return toSparqlJson(rows);
   }
   return toSparqlJson(rows);
}

http::response<http::string_body> makeResponse(http::status status, const std::string& contentType, std::string body) {
   http::response<http::string_body> res{status, 11};
   res.set(http::field::server, "gengodb-sparql-endpoint");
   res.set(http::field::content_type, contentType);
   res.body() = std::move(body);
   res.prepare_payload();
   return res;
}

http::response<http::string_body> handleUpdate(EngineFacade& engine, const http::request<http::string_body>& req) {
   if (req.method() != http::verb::post)
      return makeResponse(http::status::method_not_allowed, "text/plain", "Only POST is allowed on /sparql-update\n");

   std::string contentType(req[http::field::content_type]);
   std::string body;
   if (contentType.find("application/x-www-form-urlencoded") != std::string::npos) {
      auto form = parseFormEncoded(req.body());
      auto it = form.find("update");
      if (it == form.end())
         return makeResponse(http::status::bad_request, "text/plain", "Missing required 'update' field\n");
      body = it->second;
   } else {
      body = req.body();
   }

   StatementAccumulator acc;
   auto statements = acc.feed(body + "\n;");

   std::ostringstream log;
   size_t executed = 0;
   try {
      for (auto& raw : statements) {
         std::string stmt = trim(raw);
         if (stmt.empty()) continue;
         if (classifyStatement(stmt) == StatementKind::Query)
            throw std::runtime_error("statement is not a LOAD or settings directive; SELECT queries must go to /sparql");
         engine.withLock([&](EngineState& state) {
            handleStatement(state, stmt, std::nullopt, nullptr, /*exitOnError=*/false, /*throwOnError=*/true);
         });
         executed++;
         log << "OK\n";
      }
   } catch (const std::exception& e) {
      return makeResponse(http::status::bad_request, "text/plain", std::string("Error: ") + e.what() + "\n");
   }
   if (executed == 0) return makeResponse(http::status::bad_request, "text/plain", "No statements found in request body\n");
   return makeResponse(http::status::ok, "text/plain", log.str());
}

http::response<http::string_body> handleQueryRoute(EngineFacade& engine, const http::request<http::string_body>& req, const std::string& queryString) {
   if (req.method() != http::verb::get && req.method() != http::verb::post)
      return makeResponse(http::status::method_not_allowed, "text/plain", "Only GET or POST is allowed on /sparql\n");

   auto urlParams = parseFormEncoded(queryString);
   std::optional<std::string> queryText;
   std::string contentType(req[http::field::content_type]);

   if (req.method() == http::verb::get) {
      auto it = urlParams.find("query");
      if (it != urlParams.end()) queryText = it->second;
   } else if (contentType.find("application/sparql-query") != std::string::npos) {
      queryText = req.body();
   } else {
      auto form = parseFormEncoded(req.body());
      auto it = form.find("query");
      if (it != form.end()) queryText = it->second;
   }

   if (!queryText || trim(*queryText).empty())
      return makeResponse(http::status::bad_request, "text/plain", "Missing required 'query' parameter\n");

   std::optional<std::string> defaultGraphOverride;
   auto dgIt = urlParams.find("default-graph-uri");
   if (dgIt != urlParams.end() && !dgIt->second.empty()) defaultGraphOverride = dgIt->second;

   std::string stmt = trim(*queryText);
   try {
      if (classifyStatement(stmt) != StatementKind::Query) {
         engine.withLock([&](EngineState& state) {
            handleStatement(state, stmt, defaultGraphOverride, nullptr, /*exitOnError=*/false, /*throwOnError=*/true);
         });
         return makeResponse(http::status::ok, "text/plain", "OK\n");
      }

      std::shared_ptr<arrow::Table> result;
      engine.withLock([&](EngineState& state) {
         handleStatement(state, stmt, defaultGraphOverride, execution::createTableRetriever(result), /*exitOnError=*/false, /*throwOnError=*/true);
      });
      ResultFormat fmt = negotiateFormat(std::string(req[http::field::accept]));
      return makeResponse(http::status::ok, contentTypeFor(fmt), serialize(fmt, result));
   } catch (const std::exception& e) {
      return makeResponse(http::status::bad_request, "text/plain", std::string("Error: ") + e.what() + "\n");
   }
}

http::response<http::string_body> route(EngineFacade& engine, const http::request<http::string_body>& req) {
   std::string target(req.target());
   std::string path = target;
   std::string queryString;
   auto qpos = target.find('?');
   if (qpos != std::string::npos) {
      path = target.substr(0, qpos);
      queryString = target.substr(qpos + 1);
   }
   if (path == "/sparql-update") return handleUpdate(engine, req);
   if (path == "/sparql") return handleQueryRoute(engine, req, queryString);
   return makeResponse(http::status::not_found, "text/plain", "Not found\n");
}

void handleConnection(EngineFacade& engine, tcp::socket socket) {
   try {
      beast::flat_buffer buffer;
      http::request<http::string_body> req;
      boost::system::error_code ec;
      http::read(socket, buffer, req, ec);
      if (ec) return;

      http::response<http::string_body> res;
      try {
         res = route(engine, req);
      } catch (const std::exception& e) {
         res = makeResponse(http::status::internal_server_error, "text/plain", std::string("Internal error: ") + e.what() + "\n");
      }
      res.version(req.version());
      res.keep_alive(false);
      http::write(socket, res, ec);
      socket.shutdown(tcp::socket::shutdown_send, ec);
   } catch (const std::exception& e) {
      std::cerr << "sparql-endpoint: connection error: " << e.what() << std::endl;
   }
}

void runServer(EngineFacade& engine, const std::string& host, unsigned short port) {
   asio::io_context ioc{1};
   tcp::acceptor acceptor{ioc, {asio::ip::make_address(host), port}};
   std::cout << "sparql-endpoint listening on " << host << ":" << port << std::endl;
   while (true) {
      tcp::socket socket{ioc};
      boost::system::error_code ec;
      acceptor.accept(socket, ec);
      if (ec) {
         std::cerr << "sparql-endpoint: accept error: " << ec.message() << std::endl;
         continue;
      }
      std::thread([&engine, s = std::move(socket)]() mutable {
         handleConnection(engine, std::move(s));
      }).detach();
   }
}

} // namespace

int main(int argc, char** argv) {
   if (argc == 2 && std::string(argv[1]) == "--features") {
      printFeatures();
      return 0;
   }
   if (argc <= 1) {
      std::cerr << "USAGE: sparql-endpoint <dbDir> [--host <addr>] [--port <n>]" << std::endl;
      return 1;
   }

   std::string dbDir = argv[1];
   std::string host = "0.0.0.0";
   unsigned short port = 8080;
   for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--host" && i + 1 < argc) host = argv[++i];
      else if (arg == "--port" && i + 1 < argc) port = static_cast<unsigned short>(std::stoi(argv[++i]));
   }

   EngineState state;
   state.dbDir = dbDir;
   state.session = runtime::Session::createSession(dbDir, true);
   EngineFacade engine(std::move(state));

   compiler::support::eval::init();
   auto scheduler = scheduler::startScheduler();

   runServer(engine, host, port);
   return 0;
}
