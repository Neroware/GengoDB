#include "gengodb/semantics/FuzzyTurtle.h"

#include <cctype>
#include <charconv>
#include <optional>

namespace gengodb::semantics {
namespace {

enum class Tok {
    Iri,       // <...>
    Name,      // prefixed name, blank node label, `a`, true/false, PREFIX/BASE
    String,    // "..." '...' """...""" '''...'''
    LangTag,   // @en (only directly after a string)
    Caret2,    // ^^
    Number,
    Directive, // @prefix / @base
    Degree,    // {0.4}
    Punct,     // [ ] ( ) , ; .
    End
};

struct Token {
    Tok kind;
    std::string_view text;
    size_t begin;
    size_t line;
    float degree = 1.0f;
};

bool isNameDelimiter(char c) {
    switch (c) {
        case '<': case '>': case '"': case '\'': case '{': case '}': case '[': case ']':
        case '(': case ')': case ',': case ';': case '#': case '^': case '@':
            return true;
        default:
            return std::isspace(static_cast<unsigned char>(c));
    }
}

class Lexer {
public:
    explicit Lexer(std::string_view in) : in(in) {}

    Token next() {
        skipWsAndComments();
        if (pos >= in.size()) return {Tok::End, {}, pos, line};
        const size_t start = pos;
        const size_t startLine = line;
        const char c = in[pos];
        auto make = [&](Tok kind) {
            Token t{kind, in.substr(start, pos - start), start, startLine};
            lastKind = kind;
            return t;
        };
        if (c == '<') {
            ++pos;
            while (pos < in.size() && in[pos] != '>') {
                if (in[pos] == '\n') fail("unterminated IRI");
                if (in[pos] == '\\') ++pos;
                ++pos;
            }
            if (pos >= in.size()) fail("unterminated IRI");
            ++pos;
            return make(Tok::Iri);
        }
        if (c == '"' || c == '\'') {
            lexString(c);
            return make(Tok::String);
        }
        if (c == '{') {
            Token t{Tok::Degree, {}, start, startLine};
            ++pos;
            skipWs();
            const size_t numStart = pos;
            while (pos < in.size() && (std::isdigit(static_cast<unsigned char>(in[pos])) || in[pos] == '.' || in[pos] == 'e' || in[pos] == 'E' || in[pos] == '+' || in[pos] == '-')) ++pos;
            const std::string_view num = in.substr(numStart, pos - numStart);
            skipWs();
            if (num.empty() || pos >= in.size() || in[pos] != '}') fail("expected a truth degree like {0.5} (graph blocks are not supported)");
            ++pos;
            float value = 0;
            auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), value);
            if (ec != std::errc() || ptr != num.data() + num.size()) fail("invalid truth degree '" + std::string(num) + "'");
            if (!(value >= 0.0f && value <= 1.0f)) fail("truth degree " + std::string(num) + " is outside [0,1]");
            t.text = in.substr(start, pos - start);
            t.degree = value;
            lastKind = Tok::Degree;
            return t;
        }
        if (c == '^' && pos + 1 < in.size() && in[pos + 1] == '^') {
            pos += 2;
            return make(Tok::Caret2);
        }
        if (c == '@') {
            ++pos;
            while (pos < in.size() && (std::isalnum(static_cast<unsigned char>(in[pos])) || in[pos] == '-')) ++pos;
            return make(lastKind == Tok::String ? Tok::LangTag : Tok::Directive);
        }
        if (c == '[' || c == ']' || c == '(' || c == ')' || c == ',' || c == ';' || c == '}') {
            if (c == '}') fail("unexpected '}'");
            ++pos;
            return make(Tok::Punct);
        }
        if (c == '.' && !(pos + 1 < in.size() && std::isdigit(static_cast<unsigned char>(in[pos + 1])))) {
            ++pos;
            return make(Tok::Punct);
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.') {
            lexNumber();
            return make(Tok::Number);
        }
        while (pos < in.size() && !isNameDelimiter(in[pos])) {
            if (in[pos] == '\\') ++pos;
            ++pos;
        }
        while (pos > start + 1 && in[pos - 1] == '.' && in[pos - 2] != '\\') --pos;
        if (pos == start) fail(std::string("unexpected character '") + c + "'");
        return make(Tok::Name);
    }

    [[noreturn]] void fail(const std::string& msg) const { throw FuzzyTurtleError(msg, line); }

private:
    std::string_view in;
    size_t pos = 0;
    size_t line = 1;
    Tok lastKind = Tok::End;

    void skipWs() {
        while (pos < in.size() && std::isspace(static_cast<unsigned char>(in[pos]))) {
            if (in[pos] == '\n') ++line;
            ++pos;
        }
    }
    void skipWsAndComments() {
        while (true) {
            skipWs();
            if (pos < in.size() && in[pos] == '#') {
                while (pos < in.size() && in[pos] != '\n') ++pos;
                continue;
            }
            return;
        }
    }
    void lexString(char quote) {
        const bool longString = in.substr(pos, 3) == std::string(3, quote);
        pos += longString ? 3 : 1;
        while (pos < in.size()) {
            const char c = in[pos];
            if (c == '\\') {
                pos += 2;
                continue;
            }
            if (c == '\n') {
                if (!longString) fail("unterminated string");
                ++line;
            }
            if (c == quote) {
                if (!longString) {
                    ++pos;
                    return;
                }
                if (in.substr(pos, 3) == std::string(3, quote)) {
                    pos += 3;
                    while (pos < in.size() && in[pos] == quote) ++pos;
                    return;
                }
            }
            ++pos;
        }
        fail("unterminated string");
    }
    void lexNumber() {
        if (in[pos] == '+' || in[pos] == '-') ++pos;
        while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) ++pos;
        if (pos + 1 < in.size() && in[pos] == '.' && std::isdigit(static_cast<unsigned char>(in[pos + 1]))) {
            ++pos;
            while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) ++pos;
        }
        if (pos < in.size() && (in[pos] == 'e' || in[pos] == 'E')) {
            ++pos;
            if (pos < in.size() && (in[pos] == '+' || in[pos] == '-')) ++pos;
            while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) ++pos;
        }
    }
};

class Parser {
public:
    Parser(std::string_view in, std::string& out, std::vector<float>& degrees) : lexer(in), out(out), degrees(degrees) { advance(); }

    void parseDocument() {
        while (cur.kind != Tok::End) parseStatement();
    }

private:
    Lexer lexer;
    std::string& out;
    std::vector<float>& degrees;
    Token cur{Tok::End, {}, 0, 0};
    int collectionDepth = 0;

    void advance() { cur = lexer.next(); }
    bool isPunct(char c) const { return cur.kind == Tok::Punct && cur.text[0] == c; }
    [[noreturn]] void fail(const std::string& msg) const { throw FuzzyTurtleError(msg, cur.line); }
    void expectPunct(char c) {
        if (!isPunct(c)) fail(std::string("expected '") + c + "'");
        advance();
    }
    bool isKeyword(std::string_view kw) const {
        if (cur.kind != Tok::Name || cur.text.size() != kw.size()) return false;
        for (size_t i = 0; i < kw.size(); ++i)
            if (std::toupper(static_cast<unsigned char>(cur.text[i])) != kw[i]) return false;
        return true;
    }
    void expectTerm() {
        if (cur.kind != Tok::Iri && cur.kind != Tok::Name) fail("expected an IRI or prefixed name");
        advance();
    }

    void parseStatement() {
        if (cur.kind == Tok::Directive) {
            const bool isPrefix = cur.text == "@prefix";
            if (!isPrefix && cur.text != "@base") fail("unknown directive '" + std::string(cur.text) + "'");
            advance();
            if (isPrefix) expectTerm();
            expectTerm();
            expectPunct('.');
            return;
        }
        if (isKeyword("PREFIX") || isKeyword("BASE")) {
            const bool isPrefix = isKeyword("PREFIX");
            advance();
            if (isPrefix) expectTerm();
            expectTerm();
            return;
        }
        parseTriples();
        expectPunct('.');
    }

    void parseTriples() {
        if (isPunct('[')) {
            advance();
            const bool empty = isPunct(']');
            if (!empty) parsePredicateObjectList();
            expectPunct(']');
            if (empty || !isPunct('.')) parsePredicateObjectList();
            return;
        }
        if (isPunct('(')) {
            advance();
            parseCollectionItems();
        }
        else {
            expectTerm();
        }
        parsePredicateObjectList();
    }

    void parsePredicateObjectList() {
        parseVerb();
        parseObjectList();
        while (isPunct(';')) {
            while (isPunct(';')) advance();
            if (isPunct('.') || isPunct(']') || cur.kind == Tok::End) return;
            parseVerb();
            parseObjectList();
        }
    }

    void parseVerb() { expectTerm(); }

    void parseObjectList() {
        parseObject();
        while (isPunct(',')) {
            advance();
            parseObject();
        }
    }

    void parseObject() {
        const size_t idx = degrees.size();
        degrees.push_back(1.0f);
        if (isPunct('[')) {
            advance();
            if (!isPunct(']')) parsePredicateObjectList();
            expectPunct(']');
        }
        else if (isPunct('(')) {
            advance();
            parseCollectionItems();
        }
        else if (cur.kind == Tok::String) {
            advance();
            if (cur.kind == Tok::LangTag) {
                advance();
            }
            else if (cur.kind == Tok::Caret2) {
                advance();
                expectTerm();
            }
        }
        else if (cur.kind == Tok::Iri || cur.kind == Tok::Name || cur.kind == Tok::Number) {
            advance();
        }
        else {
            fail("expected an object");
        }
        if (cur.kind == Tok::Degree) {
            if (collectionDepth > 0) fail("truth degrees inside RDF collections are not supported");
            degrees[idx] = cur.degree;
            for (size_t i = cur.begin; i < cur.begin + cur.text.size(); ++i)
                if (out[i] != '\n') out[i] = ' ';
            advance();
        }
    }

    void parseCollectionItems() {
        ++collectionDepth;
        while (!isPunct(')')) {
            if (cur.kind == Tok::End) fail("unterminated collection");
            parseObject();
            degrees.push_back(1.0f);
        }
        advance();
        --collectionDepth;
    }
};

} // namespace

FuzzyTurtleFile parseFuzzyTTL(std::string_view input, ParsingFlag rdfParseFlags) {
    std::vector<float> degrees;
    std::string turtle;
    Parser(input, turtle, degrees).parseDocument();
    std::istringstream triples(turtle);
    IStreamQuadIterator it(triples, rdfParseFlags);
    return FuzzyTurtleFile { .triples = std::move(it), .degrees = std::move(degrees)};
}

} // namespace gengodb::semantics
