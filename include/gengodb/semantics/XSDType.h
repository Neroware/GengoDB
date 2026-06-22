#ifndef GENGODB_SEMANTICS_XSDTYPES_H
#define GENGODB_SEMANTICS_XSDTYPES_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <optional>

// Enumeration of all built-in XML Schema Definition (XSD) datatypes,
// per the W3C XML Schema Part 2: Datatypes specification.
// https://www.w3.org/TR/xmlschema11-2/
//
// IMPORTANT — persistence contract:
//   * Every enumerator has an EXPLICIT integer value. Never rely on implicit
//     sequential numbering for a value that gets written to storage — adding
//     or reordering enumerators would silently reinterpret existing rows.
//   * Values are grouped into numbered ranges with deliberate gaps, so new
//     XSD types (or custom/extension types) can be added later without
//     reusing or renumbering existing values.
//   * Do not remove or renumber an existing enumerator once data has been
//     written using it. Deprecate instead (comment it as reserved).
//
// Ranges:
//   0         -> sentinel / unknown / not-an-XSD-literal
//   1   - 99  -> primitive types
//   100 - 199 -> derived string-family types
//   200 - 299 -> derived numeric-family types

namespace gengodb::semantics::xsd {

enum class Type : std::int32_t {
    Unspecified = 0, // sentinel: no type hint / non-XSD literal / null

    // ---- Primitive types (1-99) ----
    String        = 1,
    Boolean       = 2,
    Decimal       = 3,
    Float         = 4,
    Double        = 5,
    Duration      = 6,
    DateTime      = 7,
    Time          = 8,
    Date          = 9,
    GYearMonth    = 10,
    GYear         = 11,
    GMonthDay     = 12,
    GDay          = 13,
    GMonth        = 14,
    HexBinary     = 15,
    Base64Binary  = 16,
    AnyURI        = 17,
    QName         = 18,
    Notation      = 19,

    // ---- Derived types: string family (100-199) ----
    NormalizedString = 100,
    Token            = 101,
    Language         = 102,
    NMTOKEN          = 103,
    NMTOKENS         = 104,
    Name             = 105,
    NCName           = 106,
    ID               = 107,
    IDREF            = 108,
    IDREFS           = 109,
    ENTITY           = 110,
    ENTITIES         = 111,

    // ---- Derived types: numeric family (200-299) ----
    Integer            = 200,
    NonPositiveInteger = 201,
    NegativeInteger    = 202,
    Long               = 203,
    Int                = 204,
    Short              = 205,
    Byte               = 206,
    NonNegativeInteger = 207,
    UnsignedLong       = 208,
    UnsignedInt        = 209,
    UnsignedShort      = 210,
    UnsignedByte       = 211,
    PositiveInteger    = 212,

    // ---- 
};

// Explicit, storage-safe conversions to/from the int32 you persist.
inline constexpr std::int32_t to_int32(Type t) {
    return static_cast<std::int32_t>(t);
}

// Returns std::nullopt for any int32 value that doesn't map to a known Type
// (e.g. corrupt data, or a value reserved for a future version).
inline std::optional<Type> from_int32(std::int32_t value) {
    switch (static_cast<Type>(value)) {
        case Type::Unspecified:
        case Type::String: case Type::Boolean: case Type::Decimal:
        case Type::Float: case Type::Double: case Type::Duration:
        case Type::DateTime: case Type::Time: case Type::Date:
        case Type::GYearMonth: case Type::GYear: case Type::GMonthDay:
        case Type::GDay: case Type::GMonth: case Type::HexBinary:
        case Type::Base64Binary: case Type::AnyURI: case Type::QName:
        case Type::Notation:
        case Type::NormalizedString: case Type::Token: case Type::Language:
        case Type::NMTOKEN: case Type::NMTOKENS: case Type::Name:
        case Type::NCName: case Type::ID: case Type::IDREF:
        case Type::IDREFS: case Type::ENTITY: case Type::ENTITIES:
        case Type::Integer: case Type::NonPositiveInteger:
        case Type::NegativeInteger: case Type::Long: case Type::Int:
        case Type::Short: case Type::Byte: case Type::NonNegativeInteger:
        case Type::UnsignedLong: case Type::UnsignedInt:
        case Type::UnsignedShort: case Type::UnsignedByte:
        case Type::PositiveInteger:
            return static_cast<Type>(value);
        default:
            return std::nullopt;
    }
}

// Canonical XSD local name (no "xs:"/"xsd:" prefix), e.g. Type::Integer -> "integer".
inline const std::string& to_string(Type t) {
    static const std::unordered_map<Type, std::string> names = {
        {Type::Unspecified,        "unspecified"},

        {Type::String,             "string"},
        {Type::Boolean,            "boolean"},
        {Type::Decimal,            "decimal"},
        {Type::Float,              "float"},
        {Type::Double,             "double"},
        {Type::Duration,           "duration"},
        {Type::DateTime,           "dateTime"},
        {Type::Time,               "time"},
        {Type::Date,               "date"},
        {Type::GYearMonth,         "gYearMonth"},
        {Type::GYear,              "gYear"},
        {Type::GMonthDay,          "gMonthDay"},
        {Type::GDay,               "gDay"},
        {Type::GMonth,             "gMonth"},
        {Type::HexBinary,          "hexBinary"},
        {Type::Base64Binary,       "base64Binary"},
        {Type::AnyURI,             "anyURI"},
        {Type::QName,              "QName"},
        {Type::Notation,           "NOTATION"},

        {Type::NormalizedString,   "normalizedString"},
        {Type::Token,              "token"},
        {Type::Language,           "language"},
        {Type::NMTOKEN,            "NMTOKEN"},
        {Type::NMTOKENS,           "NMTOKENS"},
        {Type::Name,               "Name"},
        {Type::NCName,             "NCName"},
        {Type::ID,                 "ID"},
        {Type::IDREF,              "IDREF"},
        {Type::IDREFS,             "IDREFS"},
        {Type::ENTITY,             "ENTITY"},
        {Type::ENTITIES,           "ENTITIES"},

        {Type::Integer,            "integer"},
        {Type::NonPositiveInteger, "nonPositiveInteger"},
        {Type::NegativeInteger,    "negativeInteger"},
        {Type::Long,               "long"},
        {Type::Int,                "int"},
        {Type::Short,              "short"},
        {Type::Byte,               "byte"},
        {Type::NonNegativeInteger, "nonNegativeInteger"},
        {Type::UnsignedLong,       "unsignedLong"},
        {Type::UnsignedInt,        "unsignedInt"},
        {Type::UnsignedShort,      "unsignedShort"},
        {Type::UnsignedByte,       "unsignedByte"},
        {Type::PositiveInteger,    "positiveInteger"},
    };
    return names.at(t);
}

// Canonical XSD name -> Type. Returns std::nullopt if unknown.
inline std::optional<Type> from_string(const std::string& name) {
    static const std::unordered_map<std::string, Type> lookup = {
        {"unspecified",        Type::Unspecified},

        {"string",             Type::String},
        {"boolean",            Type::Boolean},
        {"decimal",            Type::Decimal},
        {"float",              Type::Float},
        {"double",             Type::Double},
        {"duration",           Type::Duration},
        {"dateTime",           Type::DateTime},
        {"time",               Type::Time},
        {"date",               Type::Date},
        {"gYearMonth",         Type::GYearMonth},
        {"gYear",              Type::GYear},
        {"gMonthDay",          Type::GMonthDay},
        {"gDay",               Type::GDay},
        {"gMonth",             Type::GMonth},
        {"hexBinary",          Type::HexBinary},
        {"base64Binary",       Type::Base64Binary},
        {"anyURI",             Type::AnyURI},
        {"QName",              Type::QName},
        {"NOTATION",           Type::Notation},

        {"normalizedString",   Type::NormalizedString},
        {"token",              Type::Token},
        {"language",           Type::Language},
        {"NMTOKEN",            Type::NMTOKEN},
        {"NMTOKENS",           Type::NMTOKENS},
        {"Name",               Type::Name},
        {"NCName",             Type::NCName},
        {"ID",                 Type::ID},
        {"IDREF",              Type::IDREF},
        {"IDREFS",             Type::IDREFS},
        {"ENTITY",             Type::ENTITY},
        {"ENTITIES",           Type::ENTITIES},

        {"integer",            Type::Integer},
        {"nonPositiveInteger", Type::NonPositiveInteger},
        {"negativeInteger",    Type::NegativeInteger},
        {"long",               Type::Long},
        {"int",                Type::Int},
        {"short",              Type::Short},
        {"byte",               Type::Byte},
        {"nonNegativeInteger", Type::NonNegativeInteger},
        {"unsignedLong",       Type::UnsignedLong},
        {"unsignedInt",        Type::UnsignedInt},
        {"unsignedShort",      Type::UnsignedShort},
        {"unsignedByte",       Type::UnsignedByte},
        {"positiveInteger",    Type::PositiveInteger},
    };
    auto it = lookup.find(name);
    if (it == lookup.end()) return std::nullopt;
    return it->second;
}

} // namespace gengodb::semantics::xsd

#endif // GENGODB_SEMANTICS_XSDTYPES_H
// File generated by Claude Code