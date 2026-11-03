#ifndef GENGODB_SEMANTICS_RDFFILEFORMAT_H
#define GENGODB_SEMANTICS_RDFFILEFORMAT_H

#include <rdf4cpp.hpp>
#include <rdf4cpp/parser/RDFFileParser.hpp>

namespace gengodb::semantics {
using namespace rdf4cpp::parser;

enum class RDFFileFormat {
    DEFAULT,
    BINARY,
    TURTLE,
    NTRIPLES,
    NQUADS,
    RDFXML,
    FUZZY_TURTLE
};

inline ParsingFlag getRDFParseFlags(RDFFileFormat format) {
    switch (format) {
        case RDFFileFormat::TURTLE: return ParsingFlag::Turtle;
        case RDFFileFormat::FUZZY_TURTLE: return ParsingFlag::Turtle;
        default: return (ParsingFlag) 0;
    }
}

inline std::string getRDFFileExtension(ParsingFlag flag) {
    switch (flag) {
        case ParsingFlag::Turtle: return ".ttl";
        default: return ".rdf";
    }
}

inline std::string getRDFFileExtension(RDFFileFormat format) {
    if (format == RDFFileFormat::FUZZY_TURTLE) return ".fttl";
    return getRDFFileExtension(getRDFParseFlags(format));
}

}


#endif // GENGODB_SEMANTICS_RDFFILEFORMAT_H