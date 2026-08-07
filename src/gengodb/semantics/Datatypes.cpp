#include "gengodb/semantics/Datatypes.h"

#include <rdf4cpp/IRI.hpp>

namespace gengodb::semantics::xsd {

namespace {
constexpr std::string_view kXsdNamespace = "http://www.w3.org/2001/XMLSchema#";
constexpr std::string_view kRdfLangStringIri = "http://www.w3.org/1999/02/22-rdf-syntax-ns#langString";
} // namespace

Type from_iri(const rdf4cpp::IRI& iri) {
    if (iri.null()) {
        return Type::Unspecified;
    }
    const std::string_view id = iri.identifier();
    if (id == kRdfLangStringIri) {
        return Type::LangString;
    }
    if (!id.starts_with(kXsdNamespace)) {
        return Type::Unspecified;
    }
    const std::string_view local_name = id.substr(kXsdNamespace.size());
    if (local_name.empty()) {
        return Type::Unspecified;
    }
    if (const auto type = from_string(std::string{local_name}); type.has_value()) {
        return *type;
    }
    return Type::Unspecified;
}

} // namespace gengodb::semantics::xsd