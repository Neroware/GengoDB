#ifndef GENGODB_SEMANTICS_FUZZYTURTLE_H
#define GENGODB_SEMANTICS_FUZZYTURTLE_H

#include <stdexcept>
#include <vector>

#include <rdf4cpp/parser/IStreamQuadIterator.hpp>

namespace gengodb::semantics {
using namespace rdf4cpp::parser;

struct FuzzyTurtleFile {
    IStreamQuadIterator triples;
    std::vector<float> degrees;
};

class FuzzyTurtleError : public std::runtime_error {
public:
    FuzzyTurtleError(const std::string& msg, size_t line) : std::runtime_error("fuzzy turtle, line " + std::to_string(line) + ": " + msg), line(line) {}
    size_t line;
};
FuzzyTurtleFile parseFuzzyTTL(std::string_view input, ParsingFlag rdfParseFlags);

} // namespace gengodb::semantics

#endif // GENGODB_SEMANTICS_FUZZYTURTLE_H
