#include "features.h"
#include "lingodb/compiler/mlir-support/eval.h"
#include "lingodb/execution/Execution.h"
#include "lingodb/execution/Timing.h"
#include "lingodb/scheduler/Scheduler.h"
#include "lingodb/utility/Setting.h"
#include "gengodb/semantics/RdfGraph.h"
#include "gengodb/catalog/GraphCatalogEntry.h"

#include <fstream>
#include <iostream>
#include <string>

namespace {
lingodb::utility::GlobalSetting<bool> eagerLoading("system.eager_loading", false);
} // namespace
int main(int argc, char** argv) {
   using namespace lingodb;

   if (argc == 2 && std::string(argv[1]) == "--features") {
      printFeatures();
      return 0;
   }

   if (argc <= 2) {
      std::cerr << "USAGE: run-sparql *.sparql database (does nothing currently)" << std::endl;
      return 1;
   }
   return 0;
}
