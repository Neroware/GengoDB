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
    RDFXML
};

inline ParsingFlag getRDFParseFlags(RDFFileFormat format) {
    switch (format) {
        case RDFFileFormat::TURTLE: return ParsingFlag::Turtle;
        default: return (ParsingFlag) 0;
    }
}

inline std::string getRDFFileExtension(ParsingFlag flag) {
    switch (flag) {
        case ParsingFlag::Turtle: return ".ttl";
        default: return ".rdf";
    }
}

}


#endif // GENGODB_SEMANTICS_RDFFILEFORMAT_H