#ifndef GENGODB_COMPILER_FRONTEND_SPARQLMLIPTRANSLATOR_H
#define GENGODB_COMPILER_FRONTEND_SPARQLMLIPTRANSLATOR_H

#include "lingodb/compiler/Dialect/DB/IR/DBDialect.h"
#include "lingodb/compiler/Dialect/DB/IR/DBOps.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgDialect.h"
#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOps.h"
#include "lingodb/compiler/Dialect/SubOperator/MemberManager.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorOps.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamDialect.h"
#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"
#include "lingodb/compiler/Dialect/util/UtilDialect.h"
#include "lingodb/compiler/Dialect/util/UtilOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsTypes.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/Variant/VariantDialect.h"
#include "gengodb/compiler/Dialect/Variant/VariantOps.h"
#include "gengodb/compiler/frontend/SparqlErrors.h"
#include "gengodb/semantics/Datatypes.h"
#include "gengodb/semantics/RdfGraph.h"

#include <rdf4cpp/datatypes/xsd/time/DateTime.hpp>

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Builders.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sparql {

struct Term {
   enum class Kind { Iri, Variable, BlankNode };
   Kind kind;
   std::string value;
};

struct Triple {
   Term s, p, o;
};

struct PatternElement {
   enum class Kind { Graph, Optional, Filter, Union };
   virtual ~PatternElement() = default;
   virtual Kind kind() const = 0;
};

struct GraphPattern : PatternElement {
   std::string graphUri;
   std::vector<Triple> triples;
   Kind kind() const override { return Kind::Graph; }
};

struct OptionalPattern : PatternElement {
   std::vector<std::unique_ptr<PatternElement>> patterns;
   Kind kind() const override { return Kind::Optional; }
};

struct UnionPattern : PatternElement {
   std::vector<std::vector<std::unique_ptr<PatternElement>>> branches;
   Kind kind() const override { return Kind::Union; }
};

struct Expr {
   enum class Kind { Variable, Literal, Not, And, Or, Compare, Arith, Negate, FunctionCall, Cast };
   virtual ~Expr() = default;
   virtual Kind kind() const = 0;
};

struct VariableExpr : Expr {
   std::string name;
   Kind kind() const override { return Kind::Variable; }
};

struct LiteralExpr : Expr {
   std::string lexicalForm;
   gengodb::semantics::xsd::Type xsdType{gengodb::semantics::xsd::Type::Unspecified};
   std::string datatypeIri;
   Kind kind() const override { return Kind::Literal; }
};

struct NotExpr : Expr {
   std::unique_ptr<Expr> operand;
   Kind kind() const override { return Kind::Not; }
};

struct AndExpr : Expr {
   std::vector<std::unique_ptr<Expr>> operands;
   Kind kind() const override { return Kind::And; }
};
struct OrExpr : Expr {
   std::vector<std::unique_ptr<Expr>> operands;
   Kind kind() const override { return Kind::Or; }
};

enum class CompareOp { Eq, Neq, Lt, Lte, Gt, Gte };
struct CompareExpr : Expr {
   CompareOp op;
   std::unique_ptr<Expr> lhs, rhs;
   Kind kind() const override { return Kind::Compare; }
};

enum class ArithOp { Add, Sub, Mul, Div };
struct ArithExpr : Expr {
   ArithOp op;
   std::unique_ptr<Expr> lhs, rhs;
   Kind kind() const override { return Kind::Arith; }
};
struct NegateExpr : Expr {
   std::unique_ptr<Expr> operand;
   Kind kind() const override { return Kind::Negate; }
};

struct FunctionCallExpr : Expr {
   std::string name;
   std::vector<std::unique_ptr<Expr>> args;
   Kind kind() const override { return Kind::FunctionCall; }
};

struct CastExpr : Expr {
   std::unique_ptr<Expr> operand;
   // nullopt = STR() (or LANG(), see isLangCast)
   std::optional<gengodb::semantics::xsd::Type> targetType;
   // true iff this is LANG() rather than STR();
   bool isLangCast{false}; 
   Kind kind() const override { return Kind::Cast; }
};

struct FilterPattern : PatternElement {
   std::unique_ptr<Expr> expr;
   Kind kind() const override { return Kind::Filter; }
};

struct Query {
   std::map<std::string, std::string> prefixes;
   bool selectStar{false};
   std::vector<std::string> selectVars;
   std::vector<std::unique_ptr<PatternElement>> patterns;
   std::optional<int64_t> limit;
   std::optional<int64_t> offset;
   bool distinct{false};
   struct OrderKey { std::unique_ptr<Expr> expr; bool descending{false}; };
   std::vector<OrderKey> orderBy;
};

} // namespace sparql

enum class TK {
   Eof, IRI, PrefixedName, Variable, BlankNode,
   Keyword, LBrace, RBrace, Dot, Comma, Semicolon,
   Star, LeftParen, RightParen, StringLit, Colon, Unknown,
   Number, Eq, Neq, Lt, Lte, Gt, Gte, AndAnd, OrOr, Bang, DoubleCaret,
   Plus, Minus, Slash
};

struct Token { TK kind; std::string value; size_t line{1}; };

class Tokenizer {
   std::string src;
   size_t pos{0};
   size_t line{1};

   char cur() const { return pos < src.size() ? src[pos] : '\0'; }
   char la()  const { return pos + 1 < src.size() ? src[pos + 1] : '\0'; }
   char adv() { char c = src[pos++]; if (c == '\n') line++; return c; }

   void skipWS() {
      while (pos < src.size()) {
         if (std::isspace(static_cast<unsigned char>(cur()))) adv();
         else if (cur() == '#') { while (pos < src.size() && cur() != '\n') adv(); }
         else break;
      }
   }

   std::string readUntil(char end) {
      std::string r;
      while (pos < src.size() && cur() != end) r += adv();
      if (cur() == end) adv();
      return r;
   }

   std::string readStr(char d) {
      std::string r;
      while (pos < src.size() && cur() != d) {
         if (cur() == '\\') { adv(); r += adv(); } else r += adv();
      }
      if (cur() == d) adv();
      return r;
   }

   Token readIdent() {
      std::string name;
      while (pos < src.size() && (std::isalnum(static_cast<unsigned char>(cur())) || cur() == '_' || cur() == '-'))
         name += adv();

      if (cur() == ':' && la() != ':') {
         adv(); // consume ':'
         std::string local;
         while (pos < src.size()) {
            char c = cur();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/' || c == '#' || c == '@')
               local += adv();
            else break;
         }
         while (!local.empty() && local.back() == '.') local.pop_back();
         return {TK::PrefixedName, name + ":" + local, line};
      }
      std::string upper = name;
      std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
      return {TK::Keyword, upper, line};
   }

   public:
   explicit Tokenizer(std::string s) : src(std::move(s)) {}

   Token next() {
      skipWS();
      if (pos >= src.size()) return {TK::Eof, {}, line};
      char c = cur();
      if (c == '<') {
         size_t p = pos + 1;
         bool isIri = false;
         while (p < src.size()) {
            char pc = src[p];
            if (pc == '>') { isIri = true; break; }
            if (std::isspace(static_cast<unsigned char>(pc)) || pc == '<' || pc == '"' || pc == '{' || pc == '}' || pc == '|' || pc == '^' || pc == '`' || pc == '\\') break;
            p++;
         }
         if (isIri) { adv(); return {TK::IRI, readUntil('>'), line}; }
         adv();
         if (cur() == '=') { adv(); return {TK::Lte, "<=", line}; }
         return {TK::Lt, "<", line};
      }
      if (c == '"')  { adv(); return {TK::StringLit, readStr('"'), line}; }
      if (c == '\'') { adv(); return {TK::StringLit, readStr('\''), line}; }
      if (c == '?' || c == '$') {
         adv();
         std::string n;
         while (pos < src.size() && (std::isalnum(static_cast<unsigned char>(cur())) || cur() == '_')) n += adv();
         return {TK::Variable, n, line};
      }
      if (c == '_' && la() == ':') {
         adv(); adv();
         std::string n;
         while (pos < src.size() && (std::isalnum(static_cast<unsigned char>(cur())) || cur() == '_' || cur() == '-')) n += adv();
         return {TK::BlankNode, n, line};
      }
      if (c == ':') { adv(); return {TK::Colon, ":", line}; }
      if (c == '{') { adv(); return {TK::LBrace,    "{", line}; }
      if (c == '}') { adv(); return {TK::RBrace,    "}", line}; }
      if (c == '.') { adv(); return {TK::Dot,       ".", line}; }
      if (c == ',') { adv(); return {TK::Comma,     ",", line}; }
      if (c == ';') { adv(); return {TK::Semicolon, ";", line}; }
      if (c == '*') { adv(); return {TK::Star,      "*", line}; }
      if (c == '(') { adv(); return {TK::LeftParen, "(", line}; }
      if (c == ')') { adv(); return {TK::RightParen,")", line}; }
      if (c == '=') { adv(); return {TK::Eq, "=", line}; }
      if (c == '!') {
         adv();
         if (cur() == '=') { adv(); return {TK::Neq, "!=", line}; }
         return {TK::Bang, "!", line};
      }
      if (c == '>') {
         adv();
         if (cur() == '=') { adv(); return {TK::Gte, ">=", line}; }
         return {TK::Gt, ">", line};
      }
      if (c == '&' && la() == '&') { adv(); adv(); return {TK::AndAnd, "&&", line}; }
      if (c == '|' && la() == '|') { adv(); adv(); return {TK::OrOr, "||", line}; }
      if (c == '^' && la() == '^') { adv(); adv(); return {TK::DoubleCaret, "^^", line}; }
      if (c == '+') { adv(); return {TK::Plus,  "+", line}; }
      if (c == '-') { adv(); return {TK::Minus, "-", line}; }
      if (c == '/') { adv(); return {TK::Slash, "/", line}; }
      if (std::isdigit(static_cast<unsigned char>(c))) {
         std::string n;
         while (pos < src.size() && std::isdigit(static_cast<unsigned char>(cur()))) n += adv();
         if (cur() == '.' && std::isdigit(static_cast<unsigned char>(la()))) {
            n += adv();
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(cur()))) n += adv();
         }
         return {TK::Number, n, line};
      }
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return readIdent();
      adv(); return {TK::Unknown, std::string(1, c), line};
   }
};

class Parser {
   Tokenizer tz;
   Token tok;

   void advance() { tok = tz.next(); }
   bool is(TK k) const { return tok.kind == k; }
   bool kw(const char* w) const { return tok.kind == TK::Keyword && tok.value == w; }

   Token eat(TK k) {
      if (tok.kind != k)
         throw std::runtime_error("Parse error at line " + std::to_string(tok.line) + ": unexpected '" + tok.value + "'");
      Token t = tok; advance(); return t;
   }
   void eatKw(const char* w) {
      if (!kw(w))
         throw std::runtime_error("Expected '" + std::string(w) + "' at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
      advance();
   }

   std::string expandPrefix(const std::string& raw, const std::map<std::string, std::string>& pref) {
      auto col = raw.find(':');
      if (col == std::string::npos) return raw;
      auto it = pref.find(raw.substr(0, col));
      return (it != pref.end()) ? it->second + raw.substr(col + 1) : raw;
   }

   static std::optional<gengodb::semantics::xsd::Type> resolveXsdTypeFromIri(const std::string& iri) {
      std::string localName = iri;
      auto sep = localName.find_last_of("#/");
      if (sep != std::string::npos) localName = localName.substr(sep + 1);
      return gengodb::semantics::xsd::from_string(localName);
   }

   static gengodb::semantics::xsd::Type foldCastTargetType(gengodb::semantics::xsd::Type t) {
      using gengodb::semantics::xsd::Type;
      if (t == Type::Integer) return Type::Long;
      if (t == Type::Decimal) return Type::Double;
      return t;
   }

   static bool isSupportedCastTarget(gengodb::semantics::xsd::Type t) {
      using gengodb::semantics::xsd::Type;
      switch (t) {
         case Type::Boolean: case Type::Byte: case Type::Short: case Type::Int: case Type::Long:
         case Type::UnsignedByte: case Type::UnsignedShort: case Type::UnsignedInt: case Type::UnsignedLong:
         case Type::Float: case Type::Double:
            return true;
         default:
            return false;
      }
   }

   std::unique_ptr<sparql::Expr> parsePrimary(const std::map<std::string, std::string>& pref) {
      if (is(TK::LeftParen)) {
         advance();
         auto e = parseFilterExpr(pref);
         eat(TK::RightParen);
         return e;
      }
      if (is(TK::Variable)) {
         auto e = std::make_unique<sparql::VariableExpr>();
         e->name = tok.value; advance();
         return e;
      }
      if (is(TK::Number)) {
         auto e = std::make_unique<sparql::LiteralExpr>();
         e->lexicalForm = tok.value;
         e->xsdType = tok.value.find('.') != std::string::npos
            ? gengodb::semantics::xsd::Type::Decimal
            : gengodb::semantics::xsd::Type::Integer;
         advance();
         return e;
      }
      if (is(TK::StringLit)) {
         auto e = std::make_unique<sparql::LiteralExpr>();
         e->lexicalForm = tok.value;
         e->xsdType = gengodb::semantics::xsd::Type::String;
         advance();
         if (is(TK::DoubleCaret)) {
            advance();
            std::string dtIri;
            if (is(TK::IRI))               { dtIri = tok.value; advance(); }
            else if (is(TK::PrefixedName)) { dtIri = expandPrefix(tok.value, pref); advance(); }
            else throw std::runtime_error("Expected datatype IRI after '^^' at line " + std::to_string(tok.line));
            auto resolved = gengodb::semantics::xsd::from_iri(rdf4cpp::IRI(dtIri));
            if (resolved == gengodb::semantics::xsd::Type::AnyLiteralNode) {
               e->xsdType = gengodb::semantics::xsd::Type::AnyLiteralScalar;
               e->datatypeIri = dtIri;
            } else {
               e->xsdType = resolved;
            }
         }
         return e;
      }
      if (is(TK::PrefixedName) || is(TK::IRI)) {
         std::string iri = is(TK::IRI) ? tok.value : expandPrefix(tok.value, pref);
         size_t line = tok.line;
         advance();
         if (!is(TK::LeftParen)) {
            auto e = std::make_unique<sparql::LiteralExpr>();
            e->lexicalForm = iri;
            e->xsdType = gengodb::semantics::xsd::Type::AnyIRI;
            return e;
         }
         advance();
         auto resolved = resolveXsdTypeFromIri(iri);
         if (!resolved)
            throw sparql::UnsupportedFeatureError("Unsupported cast target datatype '" + iri + "' at line " + std::to_string(line));
         auto target = foldCastTargetType(*resolved);
         if (!isSupportedCastTarget(target))
            throw sparql::UnsupportedFeatureError("Unsupported xsd cast target '" + gengodb::semantics::xsd::to_string(*resolved) +
               "' at line " + std::to_string(line) + " (only boolean/numeric xsd cast targets are currently supported; use STR() for string conversion)");
         auto e = std::make_unique<sparql::CastExpr>();
         e->targetType = target;
         e->operand = parseFilterExpr(pref);
         eat(TK::RightParen);
         return e;
      }
      if (kw("STR") || kw("LANG")) {
         bool isLang = kw("LANG");
         advance();
         eat(TK::LeftParen);
         auto e = std::make_unique<sparql::CastExpr>();
         e->targetType = std::nullopt;
         e->isLangCast = isLang;
         e->operand = parseFilterExpr(pref);
         eat(TK::RightParen);
         return e;
      }
      if (kw("TRUE") || kw("FALSE")) {
         auto e = std::make_unique<sparql::LiteralExpr>();
         e->lexicalForm = kw("TRUE") ? "true" : "false";
         e->xsdType = gengodb::semantics::xsd::Type::Boolean;
         advance();
         return e;
      }
      if (kw("BOUND") || kw("LANGMATCHES")) {
         auto e = std::make_unique<sparql::FunctionCallExpr>();
         e->name = kw("BOUND") ? "BOUND" : "LANGMATCHES";
         advance();
         eat(TK::LeftParen);
         e->args.push_back(parseFilterExpr(pref));
         while (is(TK::Comma)) { advance(); e->args.push_back(parseFilterExpr(pref)); }
         eat(TK::RightParen);
         return e;
      }
      if (is(TK::Keyword)) {
         std::string name = tok.value;
         size_t line = tok.line;
         Token saved = tok;
         advance();
         if (is(TK::LeftParen)) {
            advance();
            auto e = std::make_unique<sparql::FunctionCallExpr>();
            e->name = name;
            if (!is(TK::RightParen)) {
               e->args.push_back(parseFilterExpr(pref));
               while (is(TK::Comma)) { advance(); e->args.push_back(parseFilterExpr(pref)); }
            }
            eat(TK::RightParen);
            return e;
         }
         throw std::runtime_error("Expected FILTER expression term at line " + std::to_string(line) + ", got '" + saved.value + "'");
      }
      throw std::runtime_error("Expected FILTER expression term at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
   }
   std::unique_ptr<sparql::Expr> parseUnaryExpr(const std::map<std::string, std::string>& pref) {
      if (is(TK::Bang)) {
         advance();
         auto e = std::make_unique<sparql::NotExpr>();
         e->operand = parseUnaryExpr(pref);
         return e;
      }
      if (is(TK::Minus)) {
         advance();
         auto e = std::make_unique<sparql::NegateExpr>();
         e->operand = parseUnaryExpr(pref);
         return e;
      }
      if (is(TK::Plus)) { advance(); return parseUnaryExpr(pref); } // unary '+' is a no-op
      return parsePrimary(pref);
   }
   std::unique_ptr<sparql::Expr> parseMultiplicativeExpr(const std::map<std::string, std::string>& pref) {
      auto lhs = parseUnaryExpr(pref);
      while (is(TK::Star) || is(TK::Slash)) {
         auto op = is(TK::Star) ? sparql::ArithOp::Mul : sparql::ArithOp::Div;
         advance();
         auto e = std::make_unique<sparql::ArithExpr>();
         e->op = op;
         e->lhs = std::move(lhs);
         e->rhs = parseUnaryExpr(pref);
         lhs = std::move(e);
      }
      return lhs;
   }
   std::unique_ptr<sparql::Expr> parseAdditiveExpr(const std::map<std::string, std::string>& pref) {
      auto lhs = parseMultiplicativeExpr(pref);
      while (is(TK::Plus) || is(TK::Minus)) {
         auto op = is(TK::Plus) ? sparql::ArithOp::Add : sparql::ArithOp::Sub;
         advance();
         auto e = std::make_unique<sparql::ArithExpr>();
         e->op = op;
         e->lhs = std::move(lhs);
         e->rhs = parseMultiplicativeExpr(pref);
         lhs = std::move(e);
      }
      return lhs;
   }
   std::unique_ptr<sparql::Expr> parseComparisonExpr(const std::map<std::string, std::string>& pref) {
      auto lhs = parseAdditiveExpr(pref);
      sparql::CompareOp op;
      if      (is(TK::Eq))  op = sparql::CompareOp::Eq;
      else if (is(TK::Neq)) op = sparql::CompareOp::Neq;
      else if (is(TK::Lt))  op = sparql::CompareOp::Lt;
      else if (is(TK::Lte)) op = sparql::CompareOp::Lte;
      else if (is(TK::Gt))  op = sparql::CompareOp::Gt;
      else if (is(TK::Gte)) op = sparql::CompareOp::Gte;
      else return lhs;
      advance();
      auto e = std::make_unique<sparql::CompareExpr>();
      e->op = op;
      e->lhs = std::move(lhs);
      e->rhs = parseAdditiveExpr(pref);
      return e;
   }
   std::unique_ptr<sparql::Expr> parseAndFilterExpr(const std::map<std::string, std::string>& pref) {
      auto first = parseComparisonExpr(pref);
      if (!is(TK::AndAnd)) return first;
      auto e = std::make_unique<sparql::AndExpr>();
      e->operands.push_back(std::move(first));
      while (is(TK::AndAnd)) { advance(); e->operands.push_back(parseComparisonExpr(pref)); }
      return e;
   }
   std::unique_ptr<sparql::Expr> parseFilterExpr(const std::map<std::string, std::string>& pref) {
      auto first = parseAndFilterExpr(pref);
      if (!is(TK::OrOr)) return first;
      auto e = std::make_unique<sparql::OrExpr>();
      e->operands.push_back(std::move(first));
      while (is(TK::OrOr)) { advance(); e->operands.push_back(parseAndFilterExpr(pref)); }
      return e;
   }

   sparql::Term parseTerm(const std::map<std::string, std::string>& pref) {
      if (is(TK::IRI))          { std::string v = tok.value; advance(); return {sparql::Term::Kind::Iri, v}; }
      if (is(TK::PrefixedName)) { std::string v = expandPrefix(tok.value, pref); advance(); return {sparql::Term::Kind::Iri, v}; }
      if (is(TK::Variable))     { std::string v = tok.value; advance(); return {sparql::Term::Kind::Variable, v}; }
      if (is(TK::BlankNode))    { std::string v = tok.value; advance(); return {sparql::Term::Kind::BlankNode, v}; }
      if (kw("A")) { advance(); return {sparql::Term::Kind::Iri, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type"}; }
      throw std::runtime_error("Expected RDF term at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
   }

   std::vector<sparql::Triple> parseTriples(const std::map<std::string, std::string>& pref) {
      std::vector<sparql::Triple> out;
      while (!is(TK::RBrace) && !is(TK::Eof) && !kw("GRAPH") && !kw("OPTIONAL") &&
             !kw("FILTER")  && !kw("UNION")  && !kw("MINUS")  && !kw("SERVICE") &&
             !kw("BIND")    && !kw("VALUES")  && !is(TK::LBrace)) {
         if (is(TK::Dot)) { advance(); continue; }
         sparql::Term s = parseTerm(pref);
         sparql::Term p = parseTerm(pref);
         sparql::Term o = parseTerm(pref);
         out.push_back({s, p, o});
         while (is(TK::Semicolon)) {
            advance();
            if (is(TK::Dot) || is(TK::RBrace) || is(TK::Eof)) break;
            sparql::Term p2 = parseTerm(pref);
            sparql::Term o2 = parseTerm(pref);
            out.push_back({s, p2, o2});
         }
         while (is(TK::Comma)) {
            advance();
            out.push_back({s, p, parseTerm(pref)});
         }
         if (is(TK::Dot)) advance();
      }
      return out;
   }

   void parseGroup(std::vector<std::unique_ptr<sparql::PatternElement>>& out,
                   const std::map<std::string, std::string>& prefixes,
                   const std::string& activeGraph) {
      eat(TK::LBrace);
      while (!is(TK::RBrace) && !is(TK::Eof)) {
         if (is(TK::Dot)) { advance(); continue; }

         if (kw("GRAPH")) {
            advance();
            std::string uri;
            if      (is(TK::IRI))          { uri = tok.value; advance(); }
            else if (is(TK::PrefixedName)) { uri = expandPrefix(tok.value, prefixes); advance(); }
            else throw std::runtime_error("Expected graph IRI after GRAPH at line " + std::to_string(tok.line));
            parseGroup(out, prefixes, uri);
            continue;
         }

         if (kw("OPTIONAL")) {
            advance();
            auto opt = std::make_unique<sparql::OptionalPattern>();
            parseGroup(opt->patterns, prefixes, activeGraph);
            out.push_back(std::move(opt));
            continue;
         }

         if (kw("FILTER")) {
            advance();
            auto expr = parseFilterExpr(prefixes);
            auto fp = std::make_unique<sparql::FilterPattern>();
            fp->expr = std::move(expr);
            out.push_back(std::move(fp));
            continue;
         }

         for (const char* op : {"MINUS","SERVICE","BIND","VALUES"}) {
            if (kw(op)) throw sparql::UnsupportedFeatureError(std::string(op) + " is not yet supported");
         }

         if (is(TK::LBrace)) {
            std::vector<std::unique_ptr<sparql::PatternElement>> firstGroup;
            parseGroup(firstGroup, prefixes, activeGraph);
            if (kw("UNION")) {
               auto up = std::make_unique<sparql::UnionPattern>();
               up->branches.push_back(std::move(firstGroup));
               while (kw("UNION")) {
                  advance();
                  std::vector<std::unique_ptr<sparql::PatternElement>> branch;
                  parseGroup(branch, prefixes, activeGraph);
                  up->branches.push_back(std::move(branch));
               }
               out.push_back(std::move(up));
            } else {
               for (auto& elem : firstGroup) out.push_back(std::move(elem));
            }
            continue;
         }

         auto triples = parseTriples(prefixes);
         if (!triples.empty()) {
            auto gp = std::make_unique<sparql::GraphPattern>();
            gp->graphUri = activeGraph;
            gp->triples = std::move(triples);
            out.push_back(std::move(gp));
         }
      }
      eat(TK::RBrace);
   }

   public:
   explicit Parser(std::string input) : tz(std::move(input)) { advance(); }

   sparql::Query parse(const std::string& defaultGraph = "") {
      sparql::Query q;
      while (kw("PREFIX") || kw("BASE")) {
         if (kw("BASE")) { advance(); if (is(TK::IRI)) advance(); continue; }
         advance();
         std::string label;
         if (is(TK::PrefixedName)) {
            label = tok.value;
            auto c = label.rfind(':');
            if (c != std::string::npos) label = label.substr(0, c);
            advance();
         } else if (is(TK::Colon)) {
            label = ""; advance();
         } else if (is(TK::Keyword)) {
            label = tok.value; advance();
            if (is(TK::Colon)) advance();
         }
         if (!is(TK::IRI))
            throw std::runtime_error("Expected IRI in PREFIX declaration at line " + std::to_string(tok.line));
         q.prefixes[label] = tok.value; advance();
      }

      for (const char* form : {"ASK", "DESCRIBE", "CONSTRUCT"}) {
         if (kw(form)) throw sparql::UnsupportedFeatureError(std::string(form) + " queries are not yet supported (only SELECT is implemented)");
      }

      eatKw("SELECT");
      if (kw("DISTINCT")) { q.distinct = true; advance(); } else if (kw("REDUCED")) { advance(); }
      if (is(TK::Star)) { advance(); q.selectStar = true; }
      else {
         while (is(TK::Variable)) { q.selectVars.push_back(tok.value); advance(); }
         if (q.selectVars.empty())
            throw std::runtime_error("SELECT requires variables or * at line " + std::to_string(tok.line));
      }

      while (kw("FROM")) { advance(); if (kw("NAMED")) advance(); if (is(TK::IRI) || is(TK::PrefixedName)) advance(); }

      if (kw("WHERE")) advance();
      parseGroup(q.patterns, q.prefixes, defaultGraph);

      if (kw("ORDER")) {
         advance();
         if (!kw("BY")) throw std::runtime_error("Expected BY after ORDER at line " + std::to_string(tok.line));
         advance();
         do {
            bool desc = false;
            std::unique_ptr<sparql::Expr> expr;
            if (kw("ASC") || kw("DESC")) {
               desc = kw("DESC");
               advance();
               eat(TK::LeftParen);
               expr = parseFilterExpr(q.prefixes);
               eat(TK::RightParen);
            } else {
               expr = parseFilterExpr(q.prefixes);
            }
            sparql::Query::OrderKey key;
            key.expr = std::move(expr);
            key.descending = desc;
            q.orderBy.push_back(std::move(key));
         } while (!kw("LIMIT") && !kw("OFFSET") && !is(TK::Eof));
      }
      for (int i = 0; i < 2 && (kw("LIMIT") || kw("OFFSET")); i++) {
         if (kw("LIMIT")) {
            if (q.limit) throw std::runtime_error("Duplicate LIMIT clause at line " + std::to_string(tok.line));
            advance();
            if (!is(TK::Number))
               throw std::runtime_error("Expected integer after LIMIT at line " + std::to_string(tok.line));
            q.limit = std::stoll(tok.value);
            advance();
         } else {
            if (q.offset) throw std::runtime_error("Duplicate OFFSET clause at line " + std::to_string(tok.line));
            advance();
            if (!is(TK::Number))
               throw std::runtime_error("Expected integer after OFFSET at line " + std::to_string(tok.line));
            q.offset = std::stoll(tok.value);
            advance();
         }
      }

      while (!is(TK::Eof)) advance();
      return q;
   }
};

namespace {
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;

// Produce a short, MLIR-identifier-safe alias from a URI using the filename.
// file://resources/ttl/coffee/coffee.ttl#rdf -> "coffee"
static std::string uriAlias(const std::string& uri) {
   std::string name = uri;
   auto hash = name.rfind('#');
   if (hash != std::string::npos) name = name.substr(0, hash);
   auto slash = name.rfind('/');
   if (slash != std::string::npos) name = name.substr(slash + 1);
   auto dot = name.rfind('.');
   if (dot != std::string::npos) name = name.substr(0, dot);
   if (name.empty()) name = "graph";
   std::string r;
   for (char c : name)
      r += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
   if (r.empty() || std::isdigit(static_cast<unsigned char>(r[0]))) r = "g_" + r;
   return r;
}

class Translator {
   mlir::MLIRContext* ctxt;
   mlir::OpBuilder& builder;
   tuples::ColumnManager& colMgr;
   subop::MemberManager& memMgr;

   std::map<std::string, std::pair<std::string, mlir::Value>> graphInfo;
   int graphCount{0};

   std::vector<std::string> allVars; // in order of first appearance
   std::map<std::string, tuples::ColumnDefAttr> varDefs;

   std::string currentVarScope{"vars"};

   std::pair<std::string, mlir::Value> namedGraph(const std::string& uri) {
      auto it = graphInfo.find(uri);
      if (it != graphInfo.end()) return it->second;

      std::string alias = uriAlias(uri);
      for (auto& [u, info] : graphInfo) {
         if (info.first == alias) { alias += "_" + std::to_string(graphCount); break; }
      }
      graphCount++;

      auto graphDef = colMgr.createDef("graphs", alias);
      graphDef.getColumn().type = gpm::GraphReferenceType::get(ctxt, mlir::StringAttr::get(ctxt, alias), mlir::StringAttr::get(ctxt, uri));
      auto loc = builder.getUnknownLoc();
      mlir::Value stream = builder.create<gpm::NamedGraphOp>(loc, graphDef);
      graphInfo[uri] = {alias, stream};
      return {alias, stream};
   }

   mlir::Attribute termAttr(const sparql::Term& term) {
      if (term.kind == sparql::Term::Kind::Iri)
         return gpm::IdentifierTermAttr::get(ctxt, mlir::StringAttr::get(ctxt, term.value));

      if (term.kind == sparql::Term::Kind::Variable) {
         auto it = varDefs.find(term.value);
         if (it == varDefs.end()) {
            auto def = colMgr.createDef(currentVarScope, term.value);
            def.getColumn().type = gpm::VariableBindingType::get(ctxt);
            varDefs[term.value] = def;
            allVars.push_back(term.value);
            return gpm::VariableTermAttr::get(ctxt, def);
         }
         return gpm::VariableTermAttr::get(ctxt, colMgr.createRef(it->second.getColumnPtr().get()));
      }

      if (term.kind == sparql::Term::Kind::BlankNode)
         return gpm::BNodeTermAttr::get(ctxt, mlir::StringAttr::get(ctxt, term.value));

      throw std::runtime_error("Unknown term kind");
   }

   mlir::Value buildBGP(const sparql::GraphPattern& gp, mlir::Value inputStream,
                        tuples::ColumnRefAttr graphRef) {
      auto loc = builder.getUnknownLoc();
      auto bgpOp = builder.create<gpm::BasicGraphPatternOp>(
         loc, tuples::TupleStreamType::get(ctxt), inputStream);

      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleStreamType::get(ctxt), loc);
      bgpOp.getPattern().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         mlir::Value cur = block->getArgument(0);
         for (const auto& triple : gp.triples) {
            auto s = termAttr(triple.s);
            auto p = termAttr(triple.p);
            auto o = termAttr(triple.o);
            cur = builder.create<gpm::TriplePatternOp>(
               loc, tuples::TupleStreamType::get(ctxt), cur, graphRef, s, p, o);
         }
         builder.create<tuples::ReturnOp>(loc, cur);
      }
      return bgpOp.getRes();
   }

   mlir::Value buildOptional(const sparql::OptionalPattern& opt, mlir::Value inputStream) {
      auto loc = builder.getUnknownLoc();
      auto optOp = builder.create<gpm::OptionalGraphPatternOp>(
         loc, tuples::TupleStreamType::get(ctxt), inputStream);

      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleStreamType::get(ctxt), loc);
      optOp.getPattern().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         mlir::Value result = buildPatternGroup(opt.patterns, block->getArgument(0));
         if (!result)
            throw sparql::UnsupportedFeatureError("OPTIONAL block contains no supported graph patterns");
         builder.create<tuples::ReturnOp>(loc, result);
      }
      return optOp.getRes();
   }

   mlir::Value buildUnionBranch(const std::vector<std::unique_ptr<sparql::PatternElement>>& branch, mlir::Value inputStream) {
      auto savedScope = currentVarScope;
      currentVarScope = colMgr.getUniqueScope("vars");
      mlir::Value result = buildPatternGroup(branch, inputStream);
      currentVarScope = savedScope;
      return result;
   }

   template <typename BuildLeft>
   mlir::Value buildUnionLevel(BuildLeft buildLeft,
                               const std::vector<std::unique_ptr<sparql::PatternElement>>& rightBranch,
                               mlir::Value inputStream) {
      auto loc = builder.getUnknownLoc();
      auto snapshotVarDefs = varDefs;
      auto snapshotAllVars = allVars;

      mlir::Value left = buildLeft(inputStream);
      if (!left)
         throw sparql::UnsupportedFeatureError("UNION branch contains no supported graph patterns");
      auto afterLeftVarDefs = varDefs;
      auto afterLeftAllVars = allVars;
      varDefs = snapshotVarDefs;
      allVars = snapshotAllVars;

      mlir::Value right = buildUnionBranch(rightBranch, inputStream);
      if (!right)
         throw sparql::UnsupportedFeatureError("UNION branch contains no supported graph patterns");
      auto afterRightVarDefs = varDefs;
      auto afterRightAllVars = allVars;

      varDefs = snapshotVarDefs;
      allVars = snapshotAllVars;
      std::vector<std::string> mergedNames;
      for (const auto& branchVars : {afterLeftAllVars, afterRightAllVars}) {
         for (const auto& name : branchVars) {
            if (snapshotVarDefs.count(name)) continue;
            if (std::find(mergedNames.begin(), mergedNames.end(), name) == mergedNames.end())
               mergedNames.push_back(name);
         }
      }

      auto scope = colMgr.getUniqueScope("union");
      llvm::SmallVector<mlir::Attribute> mapping;
      for (const auto& name : mergedNames) {
         auto leftIt = afterLeftVarDefs.find(name);
         auto rightIt = afterRightVarDefs.find(name);
         bool inLeft = leftIt != afterLeftVarDefs.end();
         bool inRight = rightIt != afterRightVarDefs.end();
         auto* leftCol = inLeft ? leftIt->second.getColumnPtr().get() : nullptr;
         auto* rightCol = inRight ? rightIt->second.getColumnPtr().get() : nullptr;
         mlir::Attribute leftEntry = leftCol ? static_cast<mlir::Attribute>(colMgr.createRef(leftCol)) : static_cast<mlir::Attribute>(mlir::UnitAttr::get(ctxt));
         mlir::Attribute rightEntry = rightCol ? static_cast<mlir::Attribute>(colMgr.createRef(rightCol)) : static_cast<mlir::Attribute>(mlir::UnitAttr::get(ctxt));

         bool nullable = !leftCol || !rightCol;
         mlir::Type mergedType = (leftCol ? leftCol : rightCol)->type;
         for (const auto* col : {leftCol, rightCol}) {
            if (auto nullableType = col ? mlir::dyn_cast<db::NullableType>(col->type) : nullptr) {
               nullable = true;
               mergedType = nullableType.getType();
            }
         }
         auto merged = colMgr.createDef(scope, name, mlir::ArrayAttr::get(ctxt, {leftEntry, rightEntry}));
         merged.getColumn().type = nullable ? db::NullableType::get(ctxt, mergedType) : mergedType;
         mapping.push_back(merged);
         varDefs[name] = merged;
         allVars.push_back(name);
      }

      return builder.create<gpm::BagOp>(loc, tuples::TupleStreamType::get(ctxt), left, right,
                                          mlir::ArrayAttr::get(ctxt, mapping))
         .getRes();
   }

   mlir::Value buildUnionUpTo(const sparql::UnionPattern& up, size_t idx, mlir::Value inputStream) {
      if (idx == 1) {
         return buildUnionLevel(
            [this, &up](mlir::Value input) { return buildUnionBranch(up.branches[0], input); },
            up.branches[1], inputStream);
      }
      return buildUnionLevel(
         [this, &up, idx](mlir::Value input) { return buildUnionUpTo(up, idx - 1, input); },
         up.branches[idx], inputStream);
   }

   mlir::Value buildUnion(const sparql::UnionPattern& up, mlir::Value inputStream) {
      if (up.branches.size() < 2)
         throw std::runtime_error("UNION requires at least two branches");
      return buildUnionUpTo(up, up.branches.size() - 1, inputStream);
   }

   static variant::VariantCmpPredicate toVariantCmpPredicate(sparql::CompareOp op) {
      switch (op) {
         case sparql::CompareOp::Eq:  return variant::VariantCmpPredicate::eq;
         case sparql::CompareOp::Neq: return variant::VariantCmpPredicate::neq;
         case sparql::CompareOp::Lt:  return variant::VariantCmpPredicate::lt;
         case sparql::CompareOp::Lte: return variant::VariantCmpPredicate::lte;
         case sparql::CompareOp::Gt:  return variant::VariantCmpPredicate::gt;
         case sparql::CompareOp::Gte: return variant::VariantCmpPredicate::gte;
      }
      throw std::runtime_error("Unknown FILTER compare operator");
   }
   static variant::VariantArithPredicate toVariantArithPredicate(sparql::ArithOp op) {
      switch (op) {
         case sparql::ArithOp::Add: return variant::VariantArithPredicate::add;
         case sparql::ArithOp::Sub: return variant::VariantArithPredicate::sub;
         case sparql::ArithOp::Mul: return variant::VariantArithPredicate::mul;
         case sparql::ArithOp::Div: return variant::VariantArithPredicate::div;
      }
      throw std::runtime_error("Unknown FILTER arithmetic operator");
   }
   mlir::Value getFilterColumn(const std::string& name, mlir::Value tupleArg) {
      auto it = varDefs.find(name);
      if (it == varDefs.end())
         throw std::runtime_error("expression references unbound variable ?" + name);
      auto ref = colMgr.createRef(it->second.getColumnPtr().get());
      return builder.create<tuples::GetColumnOp>(builder.getUnknownLoc(), ref.getColumn().type, ref, tupleArg);
   }
   mlir::Value packedDateTimeScalar(const std::string& lexicalForm, mlir::Location loc) {
      auto value = rdf4cpp::datatypes::xsd::DateTime::from_string(lexicalForm);
      int64_t packed = gengodb::semantics::RdfDatatypeFixedHelper::packDateTime(value);
      return builder.create<mlir::arith::ConstantIntOp>(loc, packed, 64);
   }
   mlir::Value literalScalar(const sparql::LiteralExpr& lit, mlir::Location loc) {
      using gengodb::semantics::xsd::Type;
      switch (lit.xsdType) {
         case Type::Boolean:
            return builder.create<mlir::arith::ConstantIntOp>(loc, lit.lexicalForm == "true" ? 1 : 0, 1);
         case Type::Byte:
            return builder.create<mlir::arith::ConstantIntOp>(loc, std::stoll(lit.lexicalForm), 8);
         case Type::Short:
            return builder.create<mlir::arith::ConstantIntOp>(loc, std::stoll(lit.lexicalForm), 16);
         case Type::Int:
            return builder.create<mlir::arith::ConstantIntOp>(loc, std::stoll(lit.lexicalForm), 32);
         case Type::Long:
         case Type::Integer:
            return builder.create<mlir::arith::ConstantIntOp>(loc, std::stoll(lit.lexicalForm), 64);
         case Type::Float:
            return builder.create<mlir::arith::ConstantOp>(loc, builder.getF32Type(), builder.getFloatAttr(builder.getF32Type(), std::stof(lit.lexicalForm)));
         case Type::Double:
         case Type::Decimal:
            return builder.create<mlir::arith::ConstantOp>(loc, builder.getF64Type(), builder.getFloatAttr(builder.getF64Type(), std::stod(lit.lexicalForm)));
         case Type::String:
         case Type::AnyIRI:
            return builder.create<db::ConstantOp>(loc, db::StringType::get(ctxt), builder.getStringAttr(lit.lexicalForm));
         case Type::AnyLiteralScalar: {
            std::string encoded;
            uint32_t dtLen = static_cast<uint32_t>(lit.datatypeIri.size());
            encoded.append(reinterpret_cast<const char*>(&dtLen), sizeof(dtLen));
            encoded.append(lit.datatypeIri);
            encoded.append(lit.lexicalForm);
            return builder.create<util::CreateConstVarLen>(loc, util::VarLen32Type::get(ctxt), builder.getStringAttr(encoded));
         }
         default:
            throw sparql::UnsupportedFeatureError("FILTER literal datatype '" + gengodb::semantics::xsd::to_string(lit.xsdType) +
               "' is not supported (only boolean, integer-family, float, double, decimal, and string literals are)");
      }
   }
   mlir::Value translateArithExpr(const sparql::Expr& expr, mlir::Value tupleArg) {
      auto loc = builder.getUnknownLoc();
      auto variantType = variant::VariantType::get(ctxt);
      switch (expr.kind()) {
         case sparql::Expr::Kind::Variable: {
            mlir::Value ref = getFilterColumn(static_cast<const sparql::VariableExpr&>(expr).name, tupleArg);
            return builder.create<gpm::GetBindingOp>(loc, variantType, ref);
         }
         case sparql::Expr::Kind::Literal: {
            const auto& lit = static_cast<const sparql::LiteralExpr&>(expr);
            if (lit.xsdType == gengodb::semantics::xsd::Type::DateTime) {
               mlir::Value packed = packedDateTimeScalar(lit.lexicalForm, loc);
               auto typeIdAttr = builder.getI32IntegerAttr(gengodb::semantics::xsd::to_int32(gengodb::semantics::xsd::Type::DateTime));
               return builder.create<variant::CreateScalarOp>(loc, variantType, packed, typeIdAttr);
            }
            if (lit.xsdType == gengodb::semantics::xsd::Type::AnyIRI) {
               mlir::Value scalar = literalScalar(lit, loc);
               auto typeIdAttr = builder.getI32IntegerAttr(gengodb::semantics::xsd::to_int32(gengodb::semantics::xsd::Type::AnyIRI));
               return builder.create<variant::CreateScalarOp>(loc, variantType, scalar, typeIdAttr);
            }
            if (lit.xsdType == gengodb::semantics::xsd::Type::AnyLiteralScalar) {
               mlir::Value scalar = literalScalar(lit, loc);
               auto typeIdAttr = builder.getI32IntegerAttr(gengodb::semantics::xsd::to_int32(gengodb::semantics::xsd::Type::AnyLiteralScalar));
               return builder.create<variant::CreateScalarOp>(loc, variantType, scalar, typeIdAttr);
            }
            mlir::Value scalar = literalScalar(lit, loc);
            return builder.create<variant::CreateScalarOp>(loc, variantType, scalar);
         }
         case sparql::Expr::Kind::Negate: {
            const auto& e = static_cast<const sparql::NegateExpr&>(expr);
            mlir::Value operand = translateArithExpr(*e.operand, tupleArg);
            mlir::Value zeroScalar = builder.create<mlir::arith::ConstantIntOp>(loc, 0, 64);
            mlir::Value zero = builder.create<variant::CreateScalarOp>(loc, variantType, zeroScalar);
            return builder.create<variant::ArithOp>(loc, variantType, variant::VariantArithPredicate::sub, zero, operand);
         }
         case sparql::Expr::Kind::Arith: {
            const auto& e = static_cast<const sparql::ArithExpr&>(expr);
            mlir::Value lhs = translateArithExpr(*e.lhs, tupleArg);
            mlir::Value rhs = translateArithExpr(*e.rhs, tupleArg);
            return builder.create<variant::ArithOp>(loc, variantType, toVariantArithPredicate(e.op), lhs, rhs);
         }
         case sparql::Expr::Kind::Cast: {
            const auto& e = static_cast<const sparql::CastExpr&>(expr);
            mlir::Value operand = translateArithExpr(*e.operand, tupleArg);
            if (e.isLangCast) {
               auto castOp = builder.create<variant::StrCastOp>(loc, variantType, operand);
               castOp->setAttr("isLangCast", builder.getUnitAttr());
               return castOp;
            }
            if (!e.targetType.has_value())
               return builder.create<variant::StrCastOp>(loc, variantType, operand);
            mlir::Value typeId = builder.create<mlir::arith::ConstantIntOp>(loc, gengodb::semantics::xsd::to_int32(*e.targetType), 32);
            return builder.create<variant::CastOp>(loc, variantType, operand, typeId);
         }
         default:
            throw std::runtime_error("Expected a numeric FILTER expression (variable, literal, arithmetic, cast, or unary '-')");
      }
   }
   mlir::Value translateCompareExpr(const sparql::CompareExpr& e, mlir::Value tupleArg) {
      auto loc = builder.getUnknownLoc();
      auto resType = db::NullableType::get(ctxt, builder.getI1Type());
      mlir::Value lhs = translateArithExpr(*e.lhs, tupleArg);
      mlir::Value rhs = translateArithExpr(*e.rhs, tupleArg);
      return builder.create<variant::CmpOp>(loc, resType, toVariantCmpPredicate(e.op), lhs, rhs);
   }
   mlir::Value translatePredicateCall(const sparql::FunctionCallExpr& fc, mlir::Value tupleArg) {
      auto loc = builder.getUnknownLoc();
      auto resType = db::NullableType::get(ctxt, builder.getI1Type());
      if (fc.name == "BOUND") {
         if (fc.args.size() != 1 || fc.args[0]->kind() != sparql::Expr::Kind::Variable)
            throw std::runtime_error("BOUND() requires exactly one variable argument, e.g. BOUND(?x)");
         mlir::Value var = translateArithExpr(*fc.args[0], tupleArg);
         auto pred = builder.getArrayAttr({builder.getStringAttr("BOUND")});
         return builder.create<variant::PredicateOp>(loc, resType, var, pred);
      }
      if (fc.name == "LANGMATCHES") {
         if (fc.args.size() != 2)
            throw std::runtime_error("LANGMATCHES() requires exactly two arguments, e.g. LANGMATCHES(?x, \"en\")");
         mlir::Value var = translateArithExpr(*fc.args[0], tupleArg);
         if (fc.args[1]->kind() != sparql::Expr::Kind::Literal)
            throw std::runtime_error("LANGMATCHES()'s second argument must be a string literal language range");
         const auto& rangeLit = static_cast<const sparql::LiteralExpr&>(*fc.args[1]);
         if (rangeLit.xsdType != gengodb::semantics::xsd::Type::String)
            throw std::runtime_error("LANGMATCHES()'s second argument must be a string literal language range");
         auto pred = builder.getArrayAttr({builder.getStringAttr("LANGMATCHES"), builder.getStringAttr(rangeLit.lexicalForm)});
         return builder.create<variant::PredicateOp>(loc, resType, var, pred);
      }
      throw sparql::UnsupportedFeatureError("Unsupported FILTER function '" + fc.name + "'");
   }
   mlir::Value translateFilterExpr(const sparql::Expr& expr, mlir::Value tupleArg) {
      auto loc = builder.getUnknownLoc();
      switch (expr.kind()) {
         case sparql::Expr::Kind::Not: {
            const auto& e = static_cast<const sparql::NotExpr&>(expr);
            return builder.create<db::NotOp>(loc, translateFilterExpr(*e.operand, tupleArg));
         }
         case sparql::Expr::Kind::And: {
            const auto& e = static_cast<const sparql::AndExpr&>(expr);
            llvm::SmallVector<mlir::Value> vals;
            for (const auto& o : e.operands) vals.push_back(translateFilterExpr(*o, tupleArg));
            return builder.create<db::AndOp>(loc, vals);
         }
         case sparql::Expr::Kind::Or: {
            const auto& e = static_cast<const sparql::OrExpr&>(expr);
            llvm::SmallVector<mlir::Value> vals;
            for (const auto& o : e.operands) vals.push_back(translateFilterExpr(*o, tupleArg));
            return builder.create<db::OrOp>(loc, vals);
         }
         case sparql::Expr::Kind::Compare:
            return translateCompareExpr(static_cast<const sparql::CompareExpr&>(expr), tupleArg);
         case sparql::Expr::Kind::FunctionCall:
            return translatePredicateCall(static_cast<const sparql::FunctionCallExpr&>(expr), tupleArg);
         default:
            throw sparql::UnsupportedFeatureError("Unsupported FILTER expression (only comparisons, &&, ||, !, arithmetic +-*/, and BOUND()/LANGMATCHES() are implemented)");
      }
   }
   mlir::Value buildFilter(const sparql::FilterPattern& fp, mlir::Value inputStream) {
      auto loc = builder.getUnknownLoc();
      auto selOp = builder.create<gpm::FilterOp>(loc, tuples::TupleStreamType::get(ctxt), inputStream);
      auto* block = new mlir::Block;
      auto tupleArg = block->addArgument(tuples::TupleType::get(ctxt), loc);
      selOp.getPredicate().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         mlir::Value pred = translateFilterExpr(*fp.expr, tupleArg);
         builder.create<tuples::ReturnOp>(loc, pred);
      }
      return selOp.getRes();
   }

   mlir::Value buildOrderByMap(const std::vector<sparql::Query::OrderKey>& orderBy, mlir::Value inputStream,
                               llvm::SmallVector<tuples::ColumnRefAttr>& outRefs) {
      auto loc = builder.getUnknownLoc();
      auto variantType = variant::VariantType::get(ctxt);
      llvm::SmallVector<mlir::Attribute> computedCols;
      llvm::SmallVector<size_t> computedIdx;
      outRefs.resize(orderBy.size());
      for (size_t i = 0; i < orderBy.size(); i++) {
         if (orderBy[i].expr->kind() == sparql::Expr::Kind::Variable) {
            const auto& name = static_cast<const sparql::VariableExpr&>(*orderBy[i].expr).name;
            auto it = varDefs.find(name);
            if (it == varDefs.end())
               throw std::runtime_error("ORDER BY references unbound variable ?" + name);
            outRefs[i] = colMgr.createRef(it->second.getColumnPtr().get());
         } else {
            computedIdx.push_back(i);
         }
      }
      if (computedIdx.empty()) return inputStream;

      auto* block = new mlir::Block;
      auto tupleArg = block->addArgument(tuples::TupleType::get(ctxt), loc);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         llvm::SmallVector<mlir::Value> computedValues;
         for (size_t i : computedIdx) {
            mlir::Value v = translateArithExpr(*orderBy[i].expr, tupleArg);
            auto def = colMgr.createDef(colMgr.getUniqueScope("orderby"), "key" + std::to_string(i));
            def.getColumn().type = variantType;
            computedCols.push_back(def);
            computedValues.push_back(v);
            outRefs[i] = colMgr.createRef(def.getColumnPtr().get());
         }
         builder.create<tuples::ReturnOp>(loc, computedValues);
      }
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), inputStream, mlir::ArrayAttr::get(ctxt, computedCols));
      mapOp.getPredicate().push_back(block);
      return mapOp.getResult();
   }

   mlir::Value buildPatternGroup(const std::vector<std::unique_ptr<sparql::PatternElement>>& patterns,
                                 mlir::Value externalInput) {
      mlir::Value prevStream = externalInput;
      for (const auto& elemPtr : patterns) {
         if (elemPtr->kind() == sparql::PatternElement::Kind::Graph) {
            const auto& gp = static_cast<const sparql::GraphPattern&>(*elemPtr);
            if (gp.graphUri.empty())
               throw std::runtime_error("Triple patterns without an explicit GRAPH clause are not supported. Did you forget to define a default graph?");

            auto [alias, graphStream] = namedGraph(gp.graphUri);
            auto graphRef = colMgr.createRef("graphs", alias);

            mlir::Value input = prevStream ? prevStream : graphStream;
            prevStream = buildBGP(gp, input, graphRef);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Optional) {
            const auto& opt = static_cast<const sparql::OptionalPattern&>(*elemPtr);
            if (!prevStream)
               throw std::runtime_error("OPTIONAL cannot be the first pattern in a query");
            prevStream = buildOptional(opt, prevStream);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Filter) {
            const auto& fp = static_cast<const sparql::FilterPattern&>(*elemPtr);
            if (!prevStream)
               throw std::runtime_error("FILTER cannot be the first pattern in a query");
            prevStream = buildFilter(fp, prevStream);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Union) {
            const auto& up = static_cast<const sparql::UnionPattern&>(*elemPtr);
            prevStream = buildUnion(up, prevStream);
         }
      }
      return prevStream;
   }

   void preRegisterGraphs(const std::vector<std::unique_ptr<sparql::PatternElement>>& patterns) {
      for (const auto& elemPtr : patterns) {
         if (elemPtr->kind() == sparql::PatternElement::Kind::Graph) {
            const auto& gp = static_cast<const sparql::GraphPattern&>(*elemPtr);
            if (!gp.graphUri.empty()) namedGraph(gp.graphUri);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Optional) {
            const auto& opt = static_cast<const sparql::OptionalPattern&>(*elemPtr);
            preRegisterGraphs(opt.patterns);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Union) {
            const auto& up = static_cast<const sparql::UnionPattern&>(*elemPtr);
            for (const auto& branch : up.branches) preRegisterGraphs(branch);
         }
      }
   }

   public:
   Translator(mlir::MLIRContext* ctxt, mlir::OpBuilder& builder)
      : ctxt(ctxt), builder(builder),
        colMgr(ctxt->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager()),
        memMgr(ctxt->getLoadedDialect<subop::SubOperatorDialect>()->getMemberManager()) {}

   void translate(const sparql::Query& query) {
      auto loc = builder.getUnknownLoc();

      auto* egBlock = new mlir::Block;
      mlir::Type tableType;
      mlir::Value matResult;
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(egBlock);

         preRegisterGraphs(query.patterns);
         mlir::Value prevStream = buildPatternGroup(query.patterns, {});

         if (!prevStream)
            throw sparql::UnsupportedFeatureError("WHERE clause contains no supported graph patterns");

         std::vector<std::string> outVars = query.selectStar ? allVars : query.selectVars;

         llvm::SmallVector<subop::Member> members;
         llvm::SmallVector<mlir::Attribute> colRefs, colNames;
         for (size_t i = 0; i < outVars.size(); i++) {
            const auto& vn = outVars[i];
            if (std::find(allVars.begin(), allVars.end(), vn) == allVars.end())
               throw std::runtime_error("Selected variable ?" + vn + " is not bound in WHERE clause");
            members.push_back(memMgr.createMember("col" + std::to_string(i + 1), db::StringType::get(ctxt)));
            colRefs.push_back(colMgr.createRef(varDefs.at(vn).getColumnPtr().get()));
            colNames.push_back(mlir::StringAttr::get(ctxt, vn));
         }

         if (query.distinct) {
            prevStream = builder.create<relalg::ProjectionOp>(
               loc, relalg::SetSemantic::distinct, prevStream, mlir::ArrayAttr::get(ctxt, colRefs));
         }

         if (!query.orderBy.empty()) {
            llvm::SmallVector<tuples::ColumnRefAttr> keyRefs;
            prevStream = buildOrderByMap(query.orderBy, prevStream, keyRefs);
            llvm::SmallVector<mlir::Attribute> sortSpecs;
            for (size_t i = 0; i < query.orderBy.size(); i++) {
               sortSpecs.push_back(relalg::SortSpecificationAttr::get(
                  ctxt, keyRefs[i], query.orderBy[i].descending ? relalg::SortSpec::desc : relalg::SortSpec::asc));
            }
            prevStream = builder.create<relalg::SortOp>(
               loc, tuples::TupleStreamType::get(ctxt), prevStream, mlir::ArrayAttr::get(ctxt, sortSpecs));
         }

         if (query.offset) {
            prevStream = builder.create<relalg::OffsetOp>(
               loc, tuples::TupleStreamType::get(ctxt), static_cast<int32_t>(*query.offset), prevStream);
         }

         if (query.limit) {
            prevStream = builder.create<relalg::LimitOp>(
               loc, tuples::TupleStreamType::get(ctxt), static_cast<int32_t>(*query.limit), prevStream);
         }

         tableType = subop::LocalTableType::get(
            ctxt,
            subop::StateMembersAttr::get(ctxt, members),
            mlir::ArrayAttr::get(ctxt, colNames));

         auto mat = builder.create<relalg::MaterializeOp>(
            loc, tableType, prevStream,
            mlir::ArrayAttr::get(ctxt, colRefs),
            mlir::ArrayAttr::get(ctxt, colNames));
         matResult = mat.getResult();

         builder.create<relalg::QueryReturnOp>(loc, mlir::ValueRange{matResult});
      }

      auto execGroup = builder.create<relalg::QueryOp>(
         loc, mlir::TypeRange{tableType}, mlir::ValueRange{});
      execGroup.getRegion().push_back(egBlock);

      builder.create<subop::SetResultOp>(loc, 0, execGroup.getResults().front());
   }
};

inline void registerSparqlDialects(mlir::MLIRContext& context) {
   mlir::DialectRegistry registry;
   registry.insert<mlir::BuiltinDialect,
                   relalg::RelAlgDialect,
                   subop::SubOperatorDialect,
                   tuples::TupleStreamDialect,
                   db::DBDialect,
                   mlir::func::FuncDialect,
                   mlir::arith::ArithDialect,
                   mlir::memref::MemRefDialect,
                   util::UtilDialect,
                   mlir::scf::SCFDialect,
                   mlir::LLVM::LLVMDialect,
                   gpm::GPMDialect,
                   gsubop::GraphSubOpDialect,
                   variant::VariantDialect>();
   context.appendDialectRegistry(registry);
   context.loadAllAvailableDialects();
}

inline void translateSparqlToMLIR(const std::string& sparqlQuery, llvm::raw_ostream& out, const std::string& defaultGraph = "") {
   mlir::MLIRContext context;
   registerSparqlDialects(context);

   mlir::OpBuilder builder(&context);
   mlir::ModuleOp moduleOp = builder.create<mlir::ModuleOp>(builder.getUnknownLoc());
   builder.setInsertionPointToStart(moduleOp.getBody());

   auto* queryBlock = new mlir::Block;
   {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(queryBlock);

      Parser parser(sparqlQuery);
      sparql::Query query = parser.parse(defaultGraph);

      Translator translator(&context, builder);
      translator.translate(query);

      builder.create<mlir::func::ReturnOp>(builder.getUnknownLoc());
   }

   mlir::func::FuncOp funcOp = builder.create<mlir::func::FuncOp>(
      builder.getUnknownLoc(), "main",
      builder.getFunctionType({}, {}));
   funcOp.getBody().push_back(queryBlock);

   mlir::OpPrintingFlags flags;
   flags.assumeVerified();
   moduleOp->print(out, flags);
   moduleOp.erase();
}

constexpr const char* DEFAULT_RDF_GRAPH = "gengodb://sparql/settings/defaultGraph#rdf";

inline std::string translateSparqlToMLIRString(const std::string& sparqlQuery, const std::string& defaultGraph = DEFAULT_RDF_GRAPH) {
   std::string result;
   llvm::raw_string_ostream os(result);
   translateSparqlToMLIR(sparqlQuery, os, defaultGraph);
   return result;
}

} // anonymous namespace

#endif // GENGODB_COMPILER_FRONTEND_SPARQLMLIPTRANSLATOR_H
