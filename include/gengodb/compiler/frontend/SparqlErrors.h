#ifndef GENGODB_COMPILER_FRONTEND_SPARQLERRORS_H
#define GENGODB_COMPILER_FRONTEND_SPARQLERRORS_H

#include <stdexcept>

namespace sparql {

struct UnsupportedFeatureError : std::runtime_error {
   using std::runtime_error::runtime_error;
};

} // namespace sparql

#endif // GENGODB_COMPILER_FRONTEND_SPARQLERRORS_H
