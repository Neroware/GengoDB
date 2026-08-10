#include "features.h"
#include "linenoise.h"

#include "lingodb/compiler/mlir-support/eval.h"
#include "lingodb/scheduler/Scheduler.h"

#include "gengodb/engine/SparqlEngine.h"

#include <iostream>
#include <string>

using namespace lingodb;
using namespace gengodb::engine;

int main(int argc, char** argv) {
   if (argc == 2 && std::string(argv[1]) == "--features") {
      printFeatures();
      return 0;
   }
   if (argc <= 1) {
      std::cerr << "USAGE: sparql database" << std::endl;
      return 1;
   }
   EngineState state;
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
