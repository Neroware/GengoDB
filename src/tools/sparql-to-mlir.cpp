#include "features.h"
#include "gengodb/compiler/frontend/SparqlMLIRTranslator.h"

#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

constexpr const char* DEFAULT_GRAPH = "gengodb://sparql/settings/defaultGraph#rdf";

int main(int argc, char** argv) {
   if (argc == 2 && std::string(argv[1]) == "--features") {
      printFeatures();
      return 0;
   }
   if (argc < 2) {
      std::cerr << "USAGE: sparql-to-mlir <query.sparql>" << std::endl;
      return 1;
   }
   std::ifstream istream{argv[1]};
   if (!istream) {
      std::cerr << "Cannot open: " << argv[1] << std::endl;
      return 1;
   }
   std::stringstream buf;
   buf << istream.rdbuf();
   try {
      translateSparqlToMLIR(buf.str(), llvm::outs(), DEFAULT_GRAPH);
   } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
   }
   return 0;
}
