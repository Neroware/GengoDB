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
        // e.g. a domain-specific literal datatype like bsbm:USD.
        return Type::AnyType;
    }
    const std::string_view local_name = id.substr(kXsdNamespace.size());
    if (local_name.empty()) {
        return Type::AnyType;
    }
    if (const auto type = from_string(std::string{local_name}); type.has_value()) {
        return *type;
    }
    // Namespaced under xsd: but not one we have a specific case for.
    return Type::AnyType;
}

} // namespace gengodb::semantics::xsd