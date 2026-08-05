#include <fstream>
#include <iostream>
#include <string>

#include "features.h"
#include "lingodb/compiler/mlir-support/eval.h"
#include "lingodb/execution/Execution.h"
#include "lingodb/scheduler/Scheduler.h"

#include "gengodb/catalog/CreateRdfGraphDef.h"
#include "gengodb/catalog/GraphCatalogEntry.h"
#include "gengodb/semantics/RdfFileFormat.h"

using namespace gengodb::semantics;
using namespace gengodb::catalog;

RDFFileFormat getFormatFromExtension(const std::string& ext) {
   if (ext == ".ttl")
      return RDFFileFormat::TURTLE;
   if (ext == ".nt")
      return RDFFileFormat::NTRIPLES;
   if (ext == ".nq")
      return RDFFileFormat::NQUADS;
   if (ext == ".rdf")
      return RDFFileFormat::RDFXML;
   throw std::runtime_error("Unsupported RDF file format: " + ext);
}

#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

#include "features.h"
#include "lingodb/compiler/mlir-support/eval.h"
#include "lingodb/execution/Execution.h"
#include "lingodb/scheduler/Scheduler.h"

#include "gengodb/catalog/CreateRdfGraphDef.h"
#include "gengodb/catalog/GraphCatalogEntry.h"
#include "gengodb/catalog/GraphNodeIndexCatalogEntry.h"
#include "gengodb/semantics/RdfFileFormat.h"

using namespace gengodb::semantics;
using namespace gengodb::catalog;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
   using namespace lingodb;

   if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout << "Generates a LingoDB catalog from RDF files. Usage: gen-rdf-catalog [RDF_DIR] (--prefix [RDF_PREFIX])" << std::endl;
      return 0;
   }

   std::string rdfDir = argv[1];
   std::cout << "Generating LingoDB/GengoDB catalog in directory '" << rdfDir << "'..." << std::endl;;

   bool eagerLoading = std::getenv("LINGODB_BACKEND_ONLY");
   std::shared_ptr<runtime::Session> session = runtime::Session::createSession(rdfDir, eagerLoading);

   std::vector<std::shared_ptr<RDFGraphCatalogEntry>> rdfGraphs;

   for (const auto& entry : fs::directory_iterator(rdfDir)) {
      if (!entry.is_regular_file())
         continue;

      std::string name = entry.path().stem().string();
      if (auto catalogEntry = session->getCatalog()->getTypedEntry<RDFGraphCatalogEntry>(name)) {
         auto entry = catalogEntry.value();
         if (entry->getFormat() != RDFFileFormat::BINARY) {
            entry->ensureFullyLoaded();
         }
         rdfGraphs.push_back(entry);
         continue;
      }

      std::string ext = entry.path().extension().string();
      RDFFileFormat format;
      try {
         format = getFormatFromExtension(ext);
      } catch (...) {
         continue;
      }

      std::string filePath = entry.path().string();

      rdf4cpp::IRI iri;
      if (argc >= 4 && std::string(argv[2]) == "--prefix") {
         iri = IRI{argv[3] + entry.path().filename().string() + "#rdf"};
      }
      else {
         if (filePath[0] == '.' && filePath[1] == '/') {
            filePath = filePath.substr(2, filePath.size());
         }
         iri = IRI{"file://" + filePath + "#rdf"};
      }

      CreateRdfGraphDef def{name, iri, format};
      auto graphEntry = RDFGraphCatalogEntry::createFromCreateRdfGraphDef(def);

      session->getCatalog()->insertEntry(graphEntry);
      graphEntry->ensureFullyLoaded();
      rdfGraphs.push_back(graphEntry);
   }

   // Defensive: guarantees every collected entry's storage is actually loaded.
   for (auto& entry : rdfGraphs) {
      entry->ensureFullyLoaded();
   }

   auto indexEntry = GraphNodeIndexCatalogEntry::build(rdfGraphs);
   session->getCatalog()->insertEntry(indexEntry, /*replace=*/true);

   session->getCatalog()->setShouldPersist(true);
   session->getCatalog()->persist();

   return 0;
}
