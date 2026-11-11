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
#include "gengodb/compiler/Dialect/GPM/Transforms/Passes.h"
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
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sparql {

// The namespace of FuzzyRDF
inline constexpr const char* kFuzzyRdfNamespace = "https://www.forsocialrobots.de/ontologies/fuzzy-rdf-syntax-ns.rdf#";

struct Term {
   enum class Kind { Iri, Variable, BlankNode };
   Kind kind;
   std::string value;
};

// p+ / p* over a set of predicate IRIs, all in the same direction: iri, ^iri, (iri|...), (^iri|...)
struct PathStep {
   std::vector<std::string> iris;
   bool inverse = false;
   int64_t minLength = 1; // 1 for +, 0 for *
};

struct Triple {
   Term s, p, o;            // for a path triple, p is the first step IRI
   std::optional<PathStep> path;
};

struct PatternElement {
   enum class Kind { Graph, Optional, Filter, Union, Group, Truth, Bind };
   virtual ~PatternElement() = default;
   virtual Kind kind() const = 0;
};

struct GraphPattern : PatternElement {
   std::string graphUri;
   std::vector<Triple> triples;
   Kind kind() const override { return Kind::Graph; }
};

struct GroupPattern : PatternElement {
   std::vector<std::unique_ptr<PatternElement>> patterns;
   Kind kind() const override { return Kind::Group; }
};

struct OptionalPattern : PatternElement {
   std::vector<std::unique_ptr<PatternElement>> patterns;
   Kind kind() const override { return Kind::Optional; }
};

struct TruthPattern : PatternElement {
   std::optional<std::string> variable;
   std::optional<float> threshold;
   std::optional<std::string> normIri;
   std::optional<std::string> hedgeIri;
   std::vector<std::unique_ptr<PatternElement>> patterns;
   Kind kind() const override { return Kind::Truth; }
};

struct UnionPattern : PatternElement {
   std::vector<std::vector<std::unique_ptr<PatternElement>>> branches;
   Kind kind() const override { return Kind::Union; }
};

struct Expr {
   enum class Kind { Variable, Literal, Not, And, Or, Compare, Arith, Negate, FunctionCall, Cast, Aggregate };
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
   std::optional<gengodb::semantics::xsd::Type> targetType;
   bool isLangCast{false}; 
   Kind kind() const override { return Kind::Cast; }
};

enum class AggFn { Count, Sum, Avg, Min, Max, Sample, GroupConcat };

inline const char* aggFnName(AggFn fn) {
   switch (fn) {
      case AggFn::Count: return "COUNT";
      case AggFn::Sum: return "SUM";
      case AggFn::Avg: return "AVG";
      case AggFn::Min: return "MIN";
      case AggFn::Max: return "MAX";
      case AggFn::Sample: return "SAMPLE";
      case AggFn::GroupConcat: return "GROUP_CONCAT";
   }
   return "?";
}

inline std::string exprKey(const Expr& e);

struct AggregateExpr : Expr {
   AggFn fn{AggFn::Count};
   bool distinct{false};
   std::unique_ptr<Expr> arg;
   std::string separator{" "};
   Kind kind() const override { return Kind::Aggregate; }
   bool star() const { return !arg; }
   std::string key() const {
      std::string k = std::string(aggFnName(fn)) + (distinct ? "|distinct|" : "|all|") + (arg ? exprKey(*arg) : std::string("*"));
      if (fn == AggFn::GroupConcat) k += "|sep:" + separator;
      return k;
   }
};

inline std::string exprKey(const Expr& e) {
   switch (e.kind()) {
      case Expr::Kind::Variable: return "?" + static_cast<const VariableExpr&>(e).name;
      case Expr::Kind::Literal: {
         const auto& l = static_cast<const LiteralExpr&>(e);
         return "lit(" + std::to_string(l.lexicalForm.size()) + ":" + l.lexicalForm + "^" +
            std::to_string(gengodb::semantics::xsd::to_int32(l.xsdType)) + "^" + l.datatypeIri + ")";
      }
      case Expr::Kind::Not: return "not(" + exprKey(*static_cast<const NotExpr&>(e).operand) + ")";
      case Expr::Kind::Negate: return "neg(" + exprKey(*static_cast<const NegateExpr&>(e).operand) + ")";
      case Expr::Kind::And:
      case Expr::Kind::Or: {
         const auto& ops = e.kind() == Expr::Kind::And ? static_cast<const AndExpr&>(e).operands : static_cast<const OrExpr&>(e).operands;
         std::string k = e.kind() == Expr::Kind::And ? "and(" : "or(";
         for (const auto& o : ops) k += exprKey(*o) + ",";
         return k + ")";
      }
      case Expr::Kind::Compare: {
         const auto& c = static_cast<const CompareExpr&>(e);
         return "cmp" + std::to_string(static_cast<int>(c.op)) + "(" + exprKey(*c.lhs) + "," + exprKey(*c.rhs) + ")";
      }
      case Expr::Kind::Arith: {
         const auto& a = static_cast<const ArithExpr&>(e);
         return "arith" + std::to_string(static_cast<int>(a.op)) + "(" + exprKey(*a.lhs) + "," + exprKey(*a.rhs) + ")";
      }
      case Expr::Kind::FunctionCall: {
         const auto& f = static_cast<const FunctionCallExpr&>(e);
         std::string k = "fn:" + f.name + "(";
         for (const auto& o : f.args) k += exprKey(*o) + ",";
         return k + ")";
      }
      case Expr::Kind::Cast: {
         const auto& c = static_cast<const CastExpr&>(e);
         std::string target = c.isLangCast ? "lang" : (c.targetType ? std::to_string(gengodb::semantics::xsd::to_int32(*c.targetType)) : "str");
         return "cast:" + target + "(" + exprKey(*c.operand) + ")";
      }
      case Expr::Kind::Aggregate: return "agg(" + static_cast<const AggregateExpr&>(e).key() + ")";
   }
   return "?";
}

template <typename F>
void forEachFreeVariable(const Expr& e, F&& f) {
   switch (e.kind()) {
      case Expr::Kind::Variable: f(static_cast<const VariableExpr&>(e).name); return;
      case Expr::Kind::Literal:
      case Expr::Kind::Aggregate: return;
      case Expr::Kind::Not: forEachFreeVariable(*static_cast<const NotExpr&>(e).operand, f); return;
      case Expr::Kind::Negate: forEachFreeVariable(*static_cast<const NegateExpr&>(e).operand, f); return;
      case Expr::Kind::Cast: forEachFreeVariable(*static_cast<const CastExpr&>(e).operand, f); return;
      case Expr::Kind::And: for (const auto& o : static_cast<const AndExpr&>(e).operands) forEachFreeVariable(*o, f); return;
      case Expr::Kind::Or: for (const auto& o : static_cast<const OrExpr&>(e).operands) forEachFreeVariable(*o, f); return;
      case Expr::Kind::FunctionCall: for (const auto& o : static_cast<const FunctionCallExpr&>(e).args) forEachFreeVariable(*o, f); return;
      case Expr::Kind::Compare: {
         const auto& c = static_cast<const CompareExpr&>(e);
         forEachFreeVariable(*c.lhs, f);
         forEachFreeVariable(*c.rhs, f);
         return;
      }
      case Expr::Kind::Arith: {
         const auto& a = static_cast<const ArithExpr&>(e);
         forEachFreeVariable(*a.lhs, f);
         forEachFreeVariable(*a.rhs, f);
         return;
      }
   }
}

struct FilterPattern : PatternElement {
   std::unique_ptr<Expr> expr;
   Kind kind() const override { return Kind::Filter; }
};

struct BindPattern : PatternElement {
   std::unique_ptr<Expr> expr;
   std::string var;
   size_t line{0};
   Kind kind() const override { return Kind::Bind; }
};

inline void collectInScopeVariables(const PatternElement& e, std::set<std::string>& out);
inline void collectInScopeVariables(const std::vector<std::unique_ptr<PatternElement>>& patterns, std::set<std::string>& out) {
   for (const auto& p : patterns) collectInScopeVariables(*p, out);
}
inline void collectInScopeVariables(const PatternElement& e, std::set<std::string>& out) {
   switch (e.kind()) {
      case PatternElement::Kind::Graph:
         for (const auto& t : static_cast<const GraphPattern&>(e).triples) {
            for (const Term* term : {&t.s, &t.p, &t.o})
               if (term->kind == Term::Kind::Variable) out.insert(term->value);
         }
         return;
      case PatternElement::Kind::Group: collectInScopeVariables(static_cast<const GroupPattern&>(e).patterns, out); return;
      case PatternElement::Kind::Optional: collectInScopeVariables(static_cast<const OptionalPattern&>(e).patterns, out); return;
      case PatternElement::Kind::Union:
         for (const auto& branch : static_cast<const UnionPattern&>(e).branches) collectInScopeVariables(branch, out);
         return;
      case PatternElement::Kind::Truth: {
         const auto& tp = static_cast<const TruthPattern&>(e);
         collectInScopeVariables(tp.patterns, out);
         if (tp.variable) out.insert(*tp.variable);
         return;
      }
      case PatternElement::Kind::Bind: out.insert(static_cast<const BindPattern&>(e).var); return;
      case PatternElement::Kind::Filter: return;
   }
}

struct Query {
   std::map<std::string, std::string> prefixes;
   bool selectStar{false};
   std::vector<std::string> selectVars;
   struct SelectExpr { std::string alias; std::unique_ptr<Expr> expr; };
   std::vector<SelectExpr> selectExprs;
   std::vector<std::unique_ptr<PatternElement>> patterns;
   struct GroupKey { std::string var; std::unique_ptr<Expr> expr; };
   std::vector<GroupKey> groupBy;
   std::vector<std::unique_ptr<Expr>> having;
   std::optional<int64_t> limit;
   std::optional<int64_t> offset;
   bool distinct{false};
   struct OrderKey { std::unique_ptr<Expr> expr; bool descending{false}; };
   std::vector<OrderKey> orderBy;
   bool hasAggregateCall{false};
   bool hasAggregation() const { return !groupBy.empty() || !having.empty() || hasAggregateCall; }
   const SelectExpr* findSelectExpr(const std::string& alias) const {
      for (const auto& se : selectExprs)
         if (se.alias == alias) return &se;
      return nullptr;
   }
   bool isGroupKey(const std::string& v) const {
      for (const auto& g : groupBy)
         if (g.var == v) return true;
      return false;
   }
};

} // namespace sparql

enum class TK {
   Eof, IRI, PrefixedName, Variable, BlankNode,
   Keyword, LBrace, RBrace, Dot, Comma, Semicolon,
   Star, LeftParen, RightParen, StringLit, Colon, Unknown,
   Number, Eq, Neq, Lt, Lte, Gt, Gte, AndAnd, OrOr, Bang, DoubleCaret,
   Plus, Minus, Slash, Pipe, Caret, Question
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
         adv();
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

   Token peek() {
      size_t savedPos = pos, savedLine = line;
      Token t = next();
      pos = savedPos;
      line = savedLine;
      return t;
   }

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
         if (n.empty() && c == '?') return {TK::Question, "?", line}; // path modifier p?
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
      if (c == '|') { adv(); return {TK::Pipe, "|", line}; }
      if (c == '^') { adv(); return {TK::Caret, "^", line}; }
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
   bool aggregatesAllowed{false};
   bool sawAggregate{false};

   void advance() { tok = tz.next(); }
   bool is(TK k) const { return tok.kind == k; }
   bool kw(const char* w) const { return tok.kind == TK::Keyword && tok.value == w; }

   Token eat(TK k) {
      if (tok.kind != k)
         throw std::runtime_error("Parse error at line " + std::to_string(tok.line) + ": unexpected '" + tok.value + "'");
      Token t = tok; advance(); return t;
   }
   bool atLegacyTruthOf() {
      if (!is(TK::Variable) && !is(TK::Number)) return false;
      Token next = tz.peek();
      return next.kind == TK::Keyword && next.value == "TRUTH";
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

   static std::optional<sparql::AggFn> aggFnFromKeyword(const std::string& kwName) {
      if (kwName == "COUNT") return sparql::AggFn::Count;
      if (kwName == "SUM") return sparql::AggFn::Sum;
      if (kwName == "AVG") return sparql::AggFn::Avg;
      if (kwName == "MIN") return sparql::AggFn::Min;
      if (kwName == "MAX") return sparql::AggFn::Max;
      if (kwName == "SAMPLE") return sparql::AggFn::Sample;
      if (kwName == "GROUP_CONCAT") return sparql::AggFn::GroupConcat;
      return std::nullopt;
   }

   std::unique_ptr<sparql::Expr> parseAggregate(sparql::AggFn fn, const std::map<std::string, std::string>& pref) {
      size_t line = tok.line;
      std::string name = tok.value;
      if (!aggregatesAllowed)
         throw std::runtime_error("Aggregate " + name + "() at line " + std::to_string(line) +
                                  " is only allowed in SELECT expressions, HAVING or ORDER BY (and cannot be nested)");
      advance();
      eat(TK::LeftParen);
      auto e = std::make_unique<sparql::AggregateExpr>();
      e->fn = fn;
      if (kw("DISTINCT")) { e->distinct = true; advance(); }
      if (is(TK::Star)) {
         if (fn != sparql::AggFn::Count)
            throw std::runtime_error("'*' is only allowed as argument of COUNT (line " + std::to_string(line) + ")");
         advance();
      } else {
         aggregatesAllowed = false;
         e->arg = parseFilterExpr(pref);
         aggregatesAllowed = true;
      }
      if (fn == sparql::AggFn::GroupConcat && is(TK::Semicolon)) {
         advance();
         eatKw("SEPARATOR");
         eat(TK::Eq);
         if (!is(TK::StringLit))
            throw std::runtime_error("Expected string literal after SEPARATOR= at line " + std::to_string(tok.line));
         e->separator = tok.value;
         advance();
      }
      if (!is(TK::RightParen))
         throw std::runtime_error("Parse error in aggregate " + name + "() at line " + std::to_string(line) + ": unexpected '" + tok.value + "'");
      advance();
      sawAggregate = true;
      return e;
   }

   std::unique_ptr<sparql::Expr> parsePrimary(const std::map<std::string, std::string>& pref) {
      if (is(TK::Keyword)) {
         if (auto fn = aggFnFromKeyword(tok.value); fn && tz.peek().kind == TK::LeftParen)
            return parseAggregate(*fn, pref);
      }
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

   // verb := variable | path. Supported paths: elt+ / elt* with elt := iri | 'a' | ^iri | '(' elt ('|' elt)* ')'
   // (all alternatives in the same direction); a plain iri is an ordinary predicate.
   struct Verb {
      sparql::Term term;
      std::optional<sparql::PathStep> path;
   };
   [[noreturn]] void unsupportedPath(const std::string& what, size_t line) {
      throw sparql::UnsupportedFeatureError("Property path " + what + " is not supported at line " + std::to_string(line) +
                                            " (supported: p+, p*, (p1|p2)+, (^p)+ and their * forms)");
   }
   Verb parseVerb(const std::map<std::string, std::string>& pref) {
      if (is(TK::Variable) || is(TK::BlankNode)) return {parseTerm(pref), std::nullopt};
      size_t line = tok.line;
      std::vector<std::string> iris;
      std::optional<bool> inverse;
      bool composite = false;
      auto parseElt = [&]() {
         bool inv = false;
         if (is(TK::Caret)) { advance(); inv = true; composite = true; }
         if (is(TK::Bang)) unsupportedPath("'!' (negated property set)", line);
         if (is(TK::LeftParen)) unsupportedPath("nesting", line);
         sparql::Term t = parseTerm(pref);
         if (t.kind != sparql::Term::Kind::Iri) unsupportedPath("element that is not an IRI", line);
         if (inverse && *inverse != inv) unsupportedPath("alternative mixing ^p and p", line);
         inverse = inv;
         if (std::find(iris.begin(), iris.end(), t.value) == iris.end()) iris.push_back(t.value);
      };
      if (is(TK::LeftParen)) {
         advance();
         composite = true;
         parseElt();
         while (is(TK::Pipe)) { advance(); parseElt(); }
         if (!is(TK::RightParen)) unsupportedPath("syntax (expected ')')", line);
         advance();
      } else {
         parseElt();
      }
      // prefixed local names may contain '/', so `ex:p/ex:q` arrives as `ex:p/ex` followed by `:q`
      if (is(TK::Slash) || (is(TK::Colon) && iris.back().find('/') != std::string::npos)) unsupportedPath("sequence '/'", line);
      if (is(TK::Question)) unsupportedPath("'?'", line);
      if (is(TK::Pipe)) unsupportedPath("alternative '|' outside of (...)+ / (...)*", line);
      if (is(TK::Plus) || is(TK::Star)) {
         int64_t minLength = is(TK::Star) ? 0 : 1;
         advance();
         return {{sparql::Term::Kind::Iri, iris.front()}, sparql::PathStep{iris, *inverse, minLength}};
      }
      if (composite) unsupportedPath("'^p' / '(...)' without + or *", line);
      return {{sparql::Term::Kind::Iri, iris.front()}, std::nullopt};
   }

   std::vector<sparql::Triple> parseTriples(const std::map<std::string, std::string>& pref) {
      std::vector<sparql::Triple> out;
      while (!is(TK::RBrace) && !is(TK::Eof) && !kw("GRAPH") && !kw("OPTIONAL") &&
             !kw("FILTER")  && !kw("UNION")  && !kw("MINUS")  && !kw("SERVICE") &&
             !kw("BIND")    && !kw("VALUES")  && !is(TK::LBrace) && !kw("FUZZY_NORM") && !kw("TRUTH") && !atLegacyTruthOf()) {
         if (is(TK::Dot)) { advance(); continue; }
         sparql::Term s = parseTerm(pref);
         Verb verb = parseVerb(pref);
         sparql::Term o = parseTerm(pref);
         out.push_back({s, verb.term, o, verb.path});
         while (is(TK::Semicolon)) {
            advance();
            if (is(TK::Dot) || is(TK::RBrace) || is(TK::Eof)) break;
            verb = parseVerb(pref);
            sparql::Term o2 = parseTerm(pref);
            out.push_back({s, verb.term, o2, verb.path});
         }
         while (is(TK::Comma)) {
            advance();
            out.push_back({s, verb.term, parseTerm(pref), verb.path});
         }
         if (is(TK::Dot)) advance();
      }
      return out;
   }

   void parseGroup(std::vector<std::unique_ptr<sparql::PatternElement>>& out,
                   const std::map<std::string, std::string>& prefixes,
                   const std::string& activeGraph,
                   std::optional<std::string>* fuzzyNorm = nullptr) {
      eat(TK::LBrace);
      while (!is(TK::RBrace) && !is(TK::Eof)) {
         if (is(TK::Dot)) { advance(); continue; }

         if (atLegacyTruthOf()) {
            std::string target = is(TK::Variable) ? "?" + tok.value : tok.value;
            throw std::runtime_error("'" + target + " TRUTH OF { ... }' is no longer supported at line " + std::to_string(tok.line) +
                                     ", write 'TRUTH " + target + " OF { ... }' (optionally with a hedge: 'TRUTH " + target + " OF VERY { ... }')");
         }

         if (kw("TRUTH")) {
            advance();
            auto tp = std::make_unique<sparql::TruthPattern>();
            if (is(TK::Variable)) {
               tp->variable = tok.value;
            } else if (is(TK::Number)) {
               tp->threshold = std::stof(tok.value);
               if (*tp->threshold < 0.0f || *tp->threshold > 1.0f)
                  throw std::runtime_error("TRUTH OF threshold must be in [0, 1] at line " + std::to_string(tok.line));
            } else {
               throw std::runtime_error("Expected truth variable or threshold after TRUTH at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
            }
            advance();
            eatKw("OF");
            if (is(TK::Keyword)) {
               static const std::set<std::string> builtinHedges{"VERY", "EXTREMELY", "MORE_OR_LESS", "NOT"};
               if (!builtinHedges.count(tok.value))
                  throw std::runtime_error("Unknown fuzzy hedge '" + tok.value + "' at line " + std::to_string(tok.line) +
                                           " (builtin hedges: VERY, EXTREMELY, MORE_OR_LESS, NOT)");
               tp->hedgeIri = std::string(sparql::kFuzzyRdfNamespace) + tok.value;
               advance();
            } else if (is(TK::IRI)) {
               tp->hedgeIri = tok.value;
               advance();
            } else if (is(TK::PrefixedName)) {
               if (!prefixes.count(tok.value.substr(0, tok.value.find(':'))))
                  throw std::runtime_error("Undeclared prefix in fuzzy hedge '" + tok.value + "' at line " + std::to_string(tok.line));
               tp->hedgeIri = expandPrefix(tok.value, prefixes);
               advance();
            }
            if (!is(TK::LBrace))
               throw std::runtime_error("Expected " + std::string(tp->hedgeIri ? "'{' after fuzzy hedge" : "fuzzy hedge or '{' after TRUTH ... OF") +
                                        " at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
            parseGroup(tp->patterns, prefixes, activeGraph, &tp->normIri);
            out.push_back(std::move(tp));
            continue;
         }

         if (kw("FUZZY_NORM")) {
            if (!fuzzyNorm)
               throw std::runtime_error("FUZZY_NORM is only allowed directly inside a TRUTH OF block (line " + std::to_string(tok.line) + ")");
            if (fuzzyNorm->has_value())
               throw std::runtime_error("Duplicate FUZZY_NORM in TRUTH OF block at line " + std::to_string(tok.line));
            advance();
            if      (is(TK::IRI))          { *fuzzyNorm = tok.value; advance(); }
            else if (is(TK::PrefixedName)) { *fuzzyNorm = expandPrefix(tok.value, prefixes); advance(); }
            else throw std::runtime_error("Expected norm IRI after FUZZY_NORM at line " + std::to_string(tok.line));
            continue;
         }

         if (kw("GRAPH")) {
            advance();
            std::string uri;
            if      (is(TK::IRI))          { uri = tok.value; advance(); }
            else if (is(TK::PrefixedName)) { uri = expandPrefix(tok.value, prefixes); advance(); }
            else throw std::runtime_error("Expected graph IRI after GRAPH at line " + std::to_string(tok.line));
            auto group = std::make_unique<sparql::GroupPattern>();
            parseGroup(group->patterns, prefixes, uri);
            out.push_back(std::move(group));
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

         if (kw("BIND")) {
            size_t line = tok.line;
            advance();
            eat(TK::LeftParen);
            auto bp = std::make_unique<sparql::BindPattern>();
            bp->line = line;
            bp->expr = parseFilterExpr(prefixes);
            if (!kw("AS"))
               throw std::runtime_error("Expected AS in BIND at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
            advance();
            if (!is(TK::Variable))
               throw std::runtime_error("Expected variable after AS in BIND at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
            bp->var = tok.value;
            advance();
            eat(TK::RightParen);
            out.push_back(std::move(bp));
            continue;
         }

         for (const char* op : {"MINUS","SERVICE","VALUES"}) {
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
               auto group = std::make_unique<sparql::GroupPattern>();
               group->patterns = std::move(firstGroup);
               out.push_back(std::move(group));
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
         std::set<std::string> projected;
         while (is(TK::Variable) || is(TK::LeftParen)) {
            if (is(TK::Variable)) {
               if (!projected.insert(tok.value).second)
                  throw std::runtime_error("Variable ?" + tok.value + " is projected twice at line " + std::to_string(tok.line));
               q.selectVars.push_back(tok.value);
               advance();
               continue;
            }
            size_t line = tok.line;
            advance();
            aggregatesAllowed = true;
            auto expr = parseFilterExpr(q.prefixes);
            aggregatesAllowed = false;
            if (!kw("AS"))
               throw std::runtime_error("Expected AS in SELECT expression at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
            advance();
            if (!is(TK::Variable))
               throw std::runtime_error("Expected variable after AS at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
            std::string alias = tok.value;
            advance();
            eat(TK::RightParen);
            if (!projected.insert(alias).second)
               throw std::runtime_error("Variable ?" + alias + " is projected twice at line " + std::to_string(line));
            q.selectVars.push_back(alias);
            q.selectExprs.push_back({alias, std::move(expr)});
         }
         if (q.selectVars.empty())
            throw std::runtime_error("SELECT requires variables or * at line " + std::to_string(tok.line));
      }

      while (kw("FROM")) { advance(); if (kw("NAMED")) advance(); if (is(TK::IRI) || is(TK::PrefixedName)) advance(); }

      if (kw("WHERE")) advance();
      parseGroup(q.patterns, q.prefixes, defaultGraph);

      if (kw("GROUP")) {
         advance();
         if (!kw("BY")) throw std::runtime_error("Expected BY after GROUP at line " + std::to_string(tok.line));
         advance();
         size_t hiddenKeys = 0;
         while (!kw("HAVING") && !kw("ORDER") && !kw("LIMIT") && !kw("OFFSET") && !is(TK::Eof)) {
            sparql::Query::GroupKey key;
            if (is(TK::Variable)) {
               key.var = tok.value;
               advance();
               if (q.isGroupKey(key.var)) continue;
            } else if (is(TK::LeftParen)) {
               advance();
               key.expr = parseFilterExpr(q.prefixes);
               if (kw("AS")) {
                  advance();
                  if (!is(TK::Variable))
                     throw std::runtime_error("Expected variable after AS in GROUP BY at line " + std::to_string(tok.line) + ", got '" + tok.value + "'");
                  key.var = tok.value;
                  advance();
               }
               eat(TK::RightParen);
            } else {
               key.expr = parsePrimary(q.prefixes);
            }
            if (key.expr && key.expr->kind() == sparql::Expr::Kind::Variable && key.var.empty()) {
               key.var = static_cast<const sparql::VariableExpr&>(*key.expr).name;
               key.expr.reset();
               if (q.isGroupKey(key.var)) continue;
            }
            if (key.var.empty()) key.var = "__gk" + std::to_string(hiddenKeys++);
            else if (key.expr && q.isGroupKey(key.var))
               throw std::runtime_error("GROUP BY alias ?" + key.var + " is defined twice at line " + std::to_string(tok.line));
            q.groupBy.push_back(std::move(key));
         }
         if (q.groupBy.empty())
            throw std::runtime_error("GROUP BY requires at least one grouping key at line " + std::to_string(tok.line));
      }

      if (kw("HAVING")) {
         advance();
         aggregatesAllowed = true;
         while (is(TK::LeftParen) || (is(TK::Keyword) && !kw("ORDER") && !kw("LIMIT") && !kw("OFFSET"))) {
            q.having.push_back(parsePrimary(q.prefixes));
         }
         aggregatesAllowed = false;
         if (q.having.empty())
            throw std::runtime_error("HAVING requires at least one constraint at line " + std::to_string(tok.line));
      }

      if (kw("ORDER")) {
         advance();
         if (!kw("BY")) throw std::runtime_error("Expected BY after ORDER at line " + std::to_string(tok.line));
         advance();
         aggregatesAllowed = true;
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
         aggregatesAllowed = false;
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

      if (!is(TK::Eof))
         throw std::runtime_error("Parse error at line " + std::to_string(tok.line) + ": unexpected '" + tok.value +
                                  "' after the query (unsupported or misplaced solution modifier?)");

      q.hasAggregateCall = sawAggregate;
      if (q.hasAggregation()) {
         if (q.selectStar)
            throw std::runtime_error("SELECT * is not allowed together with GROUP BY, HAVING or aggregates");
         std::set<std::string> visible;
         for (const auto& g : q.groupBy) visible.insert(g.var);
         for (const auto& v : q.selectVars) {
            if (const auto* se = q.findSelectExpr(v)) {
               sparql::forEachFreeVariable(*se->expr, [&](const std::string& fv) {
                  if (!visible.count(fv))
                     throw std::runtime_error("Variable ?" + fv + " in the SELECT expression for ?" + v +
                                              " is neither grouped (GROUP BY) nor aggregated");
               });
               visible.insert(v);
               continue;
            }
            if (!visible.count(v))
               throw std::runtime_error("Variable ?" + v + " is projected but neither grouped (GROUP BY) nor aggregated");
         }
         for (const auto& h : q.having) {
            sparql::forEachFreeVariable(*h, [&](const std::string& fv) {
               const auto* se = q.findSelectExpr(fv);
               if (se && se->expr->kind() != sparql::Expr::Kind::Aggregate)
                  throw std::runtime_error("HAVING references ?" + fv + ", which is computed by a SELECT expression evaluated after HAVING");
            });
         }
      }
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

   std::vector<std::string> allVars;
   std::map<std::string, tuples::ColumnDefAttr> varDefs;
   int truthDepth = 0; // > 0 while translating the body of a TRUTH OF block

   std::string currentVarScope{"vars"};
   std::map<std::string, tuples::ColumnDefAttr> aggCols;
   std::set<std::string> unboundVars;

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
            if (triple.path) continue; // property paths are added on the group's stream, see buildPaths
            auto s = termAttr(triple.s);
            auto p = termAttr(triple.p);
            auto o = termAttr(triple.o);
            cur = builder.create<gpm::TriplePatternOp>(
               loc, tuples::TupleStreamType::get(ctxt), cur, graphRef, s, p, o, /*degree=*/tuples::ColumnDefAttr());
         }
         builder.create<tuples::ReturnOp>(loc, cur);
      }
      return bgpOp.getRes();
   }

   // which end anchors the path: a constant end first (one BFS source), else an end bound in this BGP
   std::optional<bool> pathAnchor(const sparql::Triple& t, const std::set<std::string>& boundHere) {
      auto isConst = [](const sparql::Term& term) { return term.kind == sparql::Term::Kind::Iri; };
      auto isBound = [&](const sparql::Term& term) { return term.kind == sparql::Term::Kind::Variable && boundHere.count(term.value); };
      if (isConst(t.s)) return true;
      if (isConst(t.o)) return false;
      if (isBound(t.s)) return true;
      if (isBound(t.o)) return false;
      return std::nullopt;
   }
   // Property paths of a GRAPH pattern, joined onto the group's stream so far (the BGP's triples and
   // every earlier element of the group). R ⋈ path is a join like any other, so the order within the
   // group doesn't change the result. Each path is placed once one of its ends is constant or bound.
   static std::string termString(const sparql::Term& term) {
      switch (term.kind) {
         case sparql::Term::Kind::Iri: return "<" + term.value + ">";
         case sparql::Term::Kind::Variable: return "?" + term.value;
         case sparql::Term::Kind::BlankNode: return "_:" + term.value;
      }
      return term.value;
   }
   mlir::Value buildPaths(const sparql::GraphPattern& gp, mlir::Value input, tuples::ColumnRefAttr graphRef, const std::set<std::string>& groupScope) {
      std::set<std::string> boundHere = groupScope;
      std::vector<const sparql::Triple*> paths;
      for (const auto& triple : gp.triples) {
         if (triple.path) {
            if (triple.s.kind == sparql::Term::Kind::BlankNode || triple.o.kind == sparql::Term::Kind::BlankNode)
               throw sparql::UnsupportedFeatureError("Blank nodes as property path ends are not supported");
            if (triple.s.kind == sparql::Term::Kind::Iri && triple.o.kind == sparql::Term::Kind::Iri)
               throw sparql::UnsupportedFeatureError("Property paths with two constant ends are not supported");
            paths.push_back(&triple);
            continue;
         }
         for (const auto* term : {&triple.s, &triple.p, &triple.o})
            if (term->kind == sparql::Term::Kind::Variable) boundHere.insert(term->value);
      }
      while (!paths.empty()) {
         auto ready = std::find_if(paths.begin(), paths.end(), [&](const sparql::Triple* t) { return pathAnchor(*t, boundHere).has_value(); });
         if (ready == paths.end()) {
            const auto& t = *paths.front();
            throw sparql::UnsupportedFeatureError("Property path between " + termString(t.s) + " and " + termString(t.o) +
                                                  " needs a constant end or an end bound earlier in the same group (a path inside OPTIONAL/{...} cannot use a variable bound outside of it)");
         }
         input = buildPath(**ready, input, graphRef, boundHere);
         paths.erase(ready);
      }
      return input;
   }
   mlir::Value buildPath(const sparql::Triple& t, mlir::Value input, tuples::ColumnRefAttr graphRef, std::set<std::string>& boundHere) {
      auto loc = builder.getUnknownLoc();
      if (truthDepth > 0)
         throw sparql::UnsupportedFeatureError("Property paths inside TRUTH OF blocks are not supported");
      bool atSubject = *pathAnchor(t, boundHere);
      const auto& anchor = atSubject ? t.s : t.o;
      const auto& target = atSubject ? t.o : t.s;
      // forward along the step edges iff anchored at the subject of a non-inverse path
      bool outgoing = atSubject != t.path->inverse;
      llvm::SmallVector<mlir::Attribute> predicates;
      for (const auto& iri : t.path->iris) predicates.push_back(gpm::IdentifierTermAttr::get(ctxt, builder.getStringAttr(iri)));
      auto anchorAttr = termAttr(anchor);
      auto targetAttr = termAttr(target); // fresh def, or a ref (bound here: reachability check; bound outside: split by gpm-unnest-patterns)
      auto direction = gpm::ExpansionDirectionAttr::get(ctxt, outgoing ? gpm::ExpansionDirection::outgoing : gpm::ExpansionDirection::incoming);
      auto pathOp = builder.create<gpm::PathExpansionOp>(loc, tuples::TupleStreamType::get(ctxt), input, graphRef, anchorAttr, targetAttr,
                                                         builder.getArrayAttr(predicates), builder.getI64IntegerAttr(t.path->minLength), direction);
      boundHere.insert(target.value);
      return pathOp.getRes();
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

   static const sparql::GraphPattern* firstGraphPattern(const std::vector<std::unique_ptr<sparql::PatternElement>>& patterns) {
      for (const auto& elemPtr : patterns) {
         switch (elemPtr->kind()) {
            case sparql::PatternElement::Kind::Graph:
               return static_cast<const sparql::GraphPattern*>(elemPtr.get());
            case sparql::PatternElement::Kind::Group:
               if (auto* gp = firstGraphPattern(static_cast<const sparql::GroupPattern&>(*elemPtr).patterns)) return gp;
               break;
            case sparql::PatternElement::Kind::Truth:
               if (auto* gp = firstGraphPattern(static_cast<const sparql::TruthPattern&>(*elemPtr).patterns)) return gp;
               break;
            case sparql::PatternElement::Kind::Optional:
               if (auto* gp = firstGraphPattern(static_cast<const sparql::OptionalPattern&>(*elemPtr).patterns)) return gp;
               break;
            case sparql::PatternElement::Kind::Union:
               for (const auto& branch : static_cast<const sparql::UnionPattern&>(*elemPtr).branches) {
                  if (auto* gp = firstGraphPattern(branch)) return gp;
               }
               break;
            default:
               break;
         }
      }
      return nullptr;
   }

   static constexpr const char* kProductNormIri = "https://example.org/product_norm";

   static gpm::Hedge resolveHedge(const std::string& iri) {
      static const std::map<std::string, gpm::Hedge> builtins{
         {std::string(sparql::kFuzzyRdfNamespace) + "VERY", gpm::Hedge::very},
         {std::string(sparql::kFuzzyRdfNamespace) + "EXTREMELY", gpm::Hedge::extremely},
         {std::string(sparql::kFuzzyRdfNamespace) + "MORE_OR_LESS", gpm::Hedge::more_or_less},
         {std::string(sparql::kFuzzyRdfNamespace) + "NOT", gpm::Hedge::negation},
      };
      if (auto it = builtins.find(iri); it != builtins.end()) return it->second;
      throw sparql::UnsupportedFeatureError("fuzzy hedge <" + iri + "> is not supported (builtin hedges: VERY, EXTREMELY, MORE_OR_LESS, NOT)");
   }

   mlir::Value buildFuzzy(const sparql::TruthPattern& tp, mlir::Value inputStream) {
      auto loc = builder.getUnknownLoc();
      if (tp.normIri && *tp.normIri != kProductNormIri)
         throw sparql::UnsupportedFeatureError("FUZZY_NORM <" + *tp.normIri + "> is not supported (only <" + kProductNormIri + "> is implemented)");
      if (tp.variable && varDefs.count(*tp.variable))
         throw sparql::UnsupportedFeatureError("TRUTH OF target ?" + *tp.variable + " is already bound");
      std::optional<gpm::Hedge> hedge;
      if (tp.hedgeIri) hedge = resolveHedge(*tp.hedgeIri);
      if (!inputStream) {
         const auto* gp = firstGraphPattern(tp.patterns);
         if (!gp || gp->graphUri.empty())
            throw std::runtime_error("Triple patterns without an explicit GRAPH clause are not supported. Did you forget to define a default graph?");
         inputStream = namedGraph(gp->graphUri).second;
      }
      auto fuzzyOp = builder.create<gpm::FuzzyGraphPatternOp>(
         loc, tuples::TupleStreamType::get(ctxt), inputStream, gpm::TNormAttr::get(ctxt, gpm::TNorm::product),
         tuples::ColumnDefAttr(), mlir::FloatAttr(), hedge ? gpm::HedgeAttr::get(ctxt, *hedge) : gpm::HedgeAttr());

      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleStreamType::get(ctxt), loc);
      fuzzyOp.getPattern().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         ++truthDepth;
         mlir::Value result = buildPatternGroup(tp.patterns, block->getArgument(0));
         --truthDepth;
         if (!result)
            throw sparql::UnsupportedFeatureError("TRUTH OF block contains no supported graph patterns");
         builder.create<tuples::ReturnOp>(loc, result);
      }
      if (tp.variable) {
         if (varDefs.count(*tp.variable))
            throw sparql::UnsupportedFeatureError("TRUTH OF target ?" + *tp.variable + " must not be used inside its own block");
         auto def = colMgr.createDef(currentVarScope, *tp.variable);
         def.getColumn().type = gpm::VariableBindingType::get(ctxt);
         varDefs[*tp.variable] = def;
         allVars.push_back(*tp.variable);
         fuzzyOp.setTruthDegrAttr(def);
      } else {
         fuzzyOp.setThresholdAttr(builder.getF32FloatAttr(*tp.threshold));
      }
      return fuzzyOp.getRes();
   }

   mlir::Value buildGroup(const sparql::GroupPattern& group, mlir::Value inputStream) {
      auto loc = builder.getUnknownLoc();
      if (!inputStream) {
         const auto* gp = firstGraphPattern(group.patterns);
         if (!gp || gp->graphUri.empty())
            throw std::runtime_error("Triple patterns without an explicit GRAPH clause are not supported. Did you forget to define a default graph?");
         inputStream = namedGraph(gp->graphUri).second;
      }
      auto groupOp = builder.create<gpm::BasicGraphPatternOp>(
         loc, tuples::TupleStreamType::get(ctxt), inputStream);

      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleStreamType::get(ctxt), loc);
      groupOp.getPattern().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         mlir::Value result = buildPatternGroup(group.patterns, block->getArgument(0));
         if (!result)
            throw sparql::UnsupportedFeatureError("Group graph pattern contains no supported graph patterns");
         builder.create<tuples::ReturnOp>(loc, result);
      }
      return groupOp.getRes();
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
   mlir::Value readTerm(tuples::ColumnRefAttr ref, mlir::Value tupleArg) {
      auto loc = builder.getUnknownLoc();
      auto variantType = variant::VariantType::get(ctxt);
      mlir::Value val = builder.create<tuples::GetColumnOp>(loc, ref.getColumn().type, ref, tupleArg);
      if (auto nullable = mlir::dyn_cast<db::NullableType>(val.getType()); nullable && mlir::isa<variant::VariantType>(nullable.getType())) {
         mlir::Value isNull = builder.create<db::IsNullOp>(loc, val);
         mlir::Value raw = builder.create<db::NullableGetVal>(loc, val);
         return builder.create<variant::UnspecifiedIfOp>(loc, variantType, isNull, raw);
      }
      if (mlir::isa<variant::VariantType>(val.getType())) return val;
      return builder.create<gpm::GetBindingOp>(loc, variantType, val);
   }
   mlir::Value termOrNull(mlir::Value v) {
      auto loc = builder.getUnknownLoc();
      auto nullableVariantType = db::NullableType::get(ctxt, variant::VariantType::get(ctxt));
      mlir::Value unspecTag = builder.create<mlir::arith::ConstantIntOp>(loc, gengodb::semantics::xsd::to_int32(gengodb::semantics::xsd::Type::Unspecified), 32);
      mlir::Value isUnspec = builder.create<variant::VariantIsAOp>(loc, builder.getI1Type(), v, unspecTag);
      return builder.create<db::AsNullableOp>(loc, nullableVariantType, v, isUnspec);
   }
   mlir::Value unboundTerm() {
      auto loc = builder.getUnknownLoc();
      mlir::Value placeholder = builder.create<variant::CreateScalarOp>(loc, variant::VariantType::get(ctxt), builder.create<mlir::arith::ConstantIntOp>(loc, 0, 64));
      mlir::Value always = builder.create<mlir::arith::ConstantIntOp>(loc, 1, 1);
      return builder.create<variant::UnspecifiedIfOp>(loc, variant::VariantType::get(ctxt), always, placeholder);
   }
   tuples::ColumnRefAttr varRef(const std::string& name) {
      auto it = varDefs.find(name);
      if (it == varDefs.end())
         throw std::runtime_error("expression references unbound variable ?" + name);
      return colMgr.createRef(it->second.getColumnPtr().get());
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
            const auto& name = static_cast<const sparql::VariableExpr&>(expr).name;
            if (unboundVars.count(name)) return unboundTerm();
            return readTerm(varRef(name), tupleArg);
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
         case sparql::Expr::Kind::Aggregate: {
            const auto& e = static_cast<const sparql::AggregateExpr&>(expr);
            auto it = aggCols.find(e.key());
            if (it == aggCols.end())
               throw std::runtime_error(std::string("Aggregate ") + sparql::aggFnName(e.fn) + "() used outside of an aggregation context");
            return readTerm(colMgr.createRef(it->second.getColumnPtr().get()), tupleArg);
         }
         default:
            throw sparql::UnsupportedFeatureError("Unsupported value expression (only variables, literals, arithmetic, casts, unary '-' and aggregates are supported here)");
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
         return builder.create<variant::PredicateOp>(loc, resType, var, mlir::ValueRange{}, pred);
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
         return builder.create<variant::PredicateOp>(loc, resType, var, mlir::ValueRange{}, pred);
      }
      if (fc.name == "STRSTARTS") {
         if (fc.args.size() != 2)
            throw std::runtime_error("STRSTARTS() requires exactly two arguments, e.g. STRSTARTS(STR(?x), \"abc\")");
         mlir::Value str = translateArithExpr(*fc.args[0], tupleArg);
         mlir::Value prefix = translateArithExpr(*fc.args[1], tupleArg);
         auto pred = builder.getArrayAttr({builder.getStringAttr("STRSTARTS")});
         return builder.create<variant::PredicateOp>(loc, resType, str, mlir::ValueRange{prefix}, pred);
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
            throw sparql::UnsupportedFeatureError("Unsupported FILTER expression (only comparisons, &&, ||, !, arithmetic +-*/, and BOUND()/LANGMATCHES()/STRSTARTS() are implemented)");
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

   static mlir::Value createUnitRelation(mlir::OpBuilder& builder, mlir::Location loc) {
      auto& columnManager = builder.getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
      auto unitDef = columnManager.createDef(columnManager.getUniqueScope("unit"), "unit");
      unitDef.getColumn().type = builder.getI32Type();
      auto row = builder.getArrayAttr({builder.getI32IntegerAttr(0)});
      return builder.create<relalg::ConstRelationOp>(loc, builder.getArrayAttr({unitDef}), builder.getArrayAttr({row}));
   }

   mlir::Value buildBind(const sparql::BindPattern& bp, mlir::Value inputStream, const std::set<std::string>& groupScope) {
      auto loc = builder.getUnknownLoc();
      if (groupScope.count(bp.var))
         throw std::runtime_error("BIND target ?" + bp.var + " at line " + std::to_string(bp.line) + " is already in scope in its group graph pattern");
      if (varDefs.count(bp.var))
         throw sparql::UnsupportedFeatureError("BIND target ?" + bp.var + " at line " + std::to_string(bp.line) +
                                               " is also bound outside of its group graph pattern (not yet supported)");
      if (!inputStream) inputStream = createUnitRelation(builder, loc);

      auto def = colMgr.createDef(colMgr.getUniqueScope("bind"), bp.var);
      def.getColumn().type = db::NullableType::get(ctxt, variant::VariantType::get(ctxt));
      auto extendOp = builder.create<gpm::ExtendOp>(loc, tuples::TupleStreamType::get(ctxt), inputStream, def);
      auto* block = new mlir::Block;
      auto tupleArg = block->addArgument(tuples::TupleType::get(ctxt), loc);
      extendOp.getExpression().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         auto savedUnbound = unboundVars;
         sparql::forEachFreeVariable(*bp.expr, [&](const std::string& v) {
            if (!groupScope.count(v)) unboundVars.insert(v);
         });
         mlir::Value value = termOrNull(translateArithExpr(*bp.expr, tupleArg));
         unboundVars = savedUnbound;
         builder.create<tuples::ReturnOp>(loc, value);
      }
      varDefs[bp.var] = def;
      allVars.push_back(bp.var);
      return extendOp.getRes();
   }

   mlir::Value buildLeadingBind(const sparql::BindPattern& bp, mlir::Value outerStream) {
      auto loc = builder.getUnknownLoc();
      auto groupOp = builder.create<gpm::BasicGraphPatternOp>(loc, tuples::TupleStreamType::get(ctxt), outerStream);
      auto* block = new mlir::Block;
      block->addArgument(tuples::TupleStreamType::get(ctxt), loc);
      groupOp.getPattern().push_back(block);
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         builder.create<tuples::ReturnOp>(loc, buildBind(bp, block->getArgument(0), {}));
      }
      return groupOp.getRes();
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
      if (patterns.size() == 1 && patterns.front()->kind() == sparql::PatternElement::Kind::Group)
         return buildPatternGroup(static_cast<const sparql::GroupPattern&>(*patterns.front()).patterns, externalInput);

      mlir::Value prevStream = externalInput;
      std::set<std::string> groupScope;
      for (const auto& elemPtr : patterns) {
         if (elemPtr->kind() == sparql::PatternElement::Kind::Graph) {
            const auto& gp = static_cast<const sparql::GraphPattern&>(*elemPtr);
            if (gp.graphUri.empty())
               throw std::runtime_error("Triple patterns without an explicit GRAPH clause are not supported. Did you forget to define a default graph?");

            auto [alias, graphStream] = namedGraph(gp.graphUri);
            auto graphRef = colMgr.createRef("graphs", alias);

            mlir::Value input = prevStream ? prevStream : graphStream;
            bool hasPaths = std::any_of(gp.triples.begin(), gp.triples.end(), [](const sparql::Triple& t) { return t.path.has_value(); });
            bool hasTriples = std::any_of(gp.triples.begin(), gp.triples.end(), [](const sparql::Triple& t) { return !t.path.has_value(); });
            prevStream = hasTriples || !hasPaths ? buildBGP(gp, input, graphRef) : input;
            if (hasPaths) prevStream = buildPaths(gp, prevStream, graphRef, groupScope);
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
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Group) {
            prevStream = buildGroup(static_cast<const sparql::GroupPattern&>(*elemPtr), prevStream);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Truth) {
            prevStream = buildFuzzy(static_cast<const sparql::TruthPattern&>(*elemPtr), prevStream);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Bind) {
            const auto& bp = static_cast<const sparql::BindPattern&>(*elemPtr);
            if (prevStream && prevStream == externalInput && prevStream.getDefiningOp())
               prevStream = buildLeadingBind(bp, prevStream);
            else
               prevStream = buildBind(bp, prevStream, groupScope);
         }
         sparql::collectInScopeVariables(*elemPtr, groupScope);
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
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Group) {
            preRegisterGraphs(static_cast<const sparql::GroupPattern&>(*elemPtr).patterns);
         } else if (elemPtr->kind() == sparql::PatternElement::Kind::Truth) {
            preRegisterGraphs(static_cast<const sparql::TruthPattern&>(*elemPtr).patterns);
         }
      }
   }

   static void collectAggregates(const sparql::Expr& e, std::vector<const sparql::AggregateExpr*>& out) {
      switch (e.kind()) {
         case sparql::Expr::Kind::Aggregate:
            out.push_back(static_cast<const sparql::AggregateExpr*>(&e));
            return;
         case sparql::Expr::Kind::Not:
            collectAggregates(*static_cast<const sparql::NotExpr&>(e).operand, out);
            return;
         case sparql::Expr::Kind::Negate:
            collectAggregates(*static_cast<const sparql::NegateExpr&>(e).operand, out);
            return;
         case sparql::Expr::Kind::Cast:
            collectAggregates(*static_cast<const sparql::CastExpr&>(e).operand, out);
            return;
         case sparql::Expr::Kind::And:
            for (const auto& o : static_cast<const sparql::AndExpr&>(e).operands) collectAggregates(*o, out);
            return;
         case sparql::Expr::Kind::Or:
            for (const auto& o : static_cast<const sparql::OrExpr&>(e).operands) collectAggregates(*o, out);
            return;
         case sparql::Expr::Kind::Compare: {
            const auto& c = static_cast<const sparql::CompareExpr&>(e);
            collectAggregates(*c.lhs, out);
            collectAggregates(*c.rhs, out);
            return;
         }
         case sparql::Expr::Kind::Arith: {
            const auto& a = static_cast<const sparql::ArithExpr&>(e);
            collectAggregates(*a.lhs, out);
            collectAggregates(*a.rhs, out);
            return;
         }
         case sparql::Expr::Kind::FunctionCall:
            for (const auto& o : static_cast<const sparql::FunctionCallExpr&>(e).args) collectAggregates(*o, out);
            return;
         default:
            return;
      }
   }

   mlir::Value buildExprMap(mlir::Value stream, const std::vector<std::pair<const sparql::Expr*, bool>>& exprs, const std::string& scope,
                            const std::string& prefix, std::vector<tuples::ColumnDefAttr>& outDefs) {
      auto loc = builder.getUnknownLoc();
      auto* block = new mlir::Block;
      auto tupleArg = block->addArgument(tuples::TupleType::get(ctxt), loc);
      llvm::SmallVector<mlir::Attribute> cols;
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         llvm::SmallVector<mlir::Value> vals;
         for (size_t i = 0; i < exprs.size(); i++) {
            auto [expr, nullable] = exprs[i];
            mlir::Value v = translateArithExpr(*expr, tupleArg);
            if (nullable) v = termOrNull(v);
            auto def = colMgr.createDef(scope, prefix + std::to_string(i));
            def.getColumn().type = v.getType();
            cols.push_back(def);
            outDefs.push_back(def);
            vals.push_back(v);
         }
         builder.create<tuples::ReturnOp>(loc, vals);
      }
      auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, builder.getArrayAttr(cols));
      mapOp.getPredicate().push_back(block);
      return mapOp.getResult();
   }

   mlir::Value buildAggregation(const sparql::Query& query, mlir::Value input) {
      auto loc = builder.getUnknownLoc();
      auto variantType = variant::VariantType::get(ctxt);
      auto nullableVariantType = db::NullableType::get(ctxt, variantType);
      auto i64Type = builder.getI64Type();
      std::string scope = colMgr.getUniqueScope("aggr");

      std::vector<const sparql::AggregateExpr*> found;
      for (const auto& se : query.selectExprs) collectAggregates(*se.expr, found);
      for (const auto& h : query.having) collectAggregates(*h, found);
      for (const auto& k : query.orderBy) collectAggregates(*k.expr, found);
      std::vector<const sparql::AggregateExpr*> aggs;
      std::set<std::string> seenKeys;
      for (const auto* a : found) {
         if (seenKeys.insert(a->key()).second) aggs.push_back(a);
      }

      for (const auto& g : query.groupBy) {
         if (g.expr) {
            if (varDefs.count(g.var))
               throw std::runtime_error("GROUP BY alias ?" + g.var + " is already bound in WHERE clause");
            sparql::forEachFreeVariable(*g.expr, [&](const std::string& v) {
               if (!varDefs.count(v)) throw std::runtime_error("GROUP BY expression references ?" + v + ", which is not bound in WHERE clause");
            });
         } else if (!varDefs.count(g.var)) {
            throw std::runtime_error("GROUP BY variable ?" + g.var + " is not bound in WHERE clause");
         }
      }
      for (const auto* a : aggs) {
         if (!a->arg) continue;
         sparql::forEachFreeVariable(*a->arg, [&](const std::string& v) {
            if (!varDefs.count(v))
               throw std::runtime_error(std::string("Aggregate ") + sparql::aggFnName(a->fn) + "() references ?" + v + ", which is not bound in WHERE clause");
         });
      }
      for (const auto& se : query.selectExprs) {
         if (varDefs.count(se.alias))
            throw std::runtime_error("SELECT alias ?" + se.alias + " is already bound in WHERE clause");
      }

      auto poisonsOnError = [](sparql::AggFn fn) { return fn == sparql::AggFn::Sum || fn == sparql::AggFn::Avg; };
      std::vector<std::pair<const sparql::Expr*, bool>> preExprs;
      std::map<std::string, size_t> preIdx; // exprKey|mode -> index into preExprs
      auto preKey = [](const sparql::Expr& e, bool nullable) { return sparql::exprKey(e) + (nullable ? "|null" : "|keep"); };
      auto addPre = [&](const sparql::Expr& e, bool nullable) {
         auto k = preKey(e, nullable);
         if (!preIdx.count(k)) {
            preIdx[k] = preExprs.size();
            preExprs.push_back({&e, nullable});
         }
      };
      for (const auto& g : query.groupBy)
         if (g.expr) addPre(*g.expr, true);
      for (const auto* a : aggs)
         if (a->arg && a->arg->kind() != sparql::Expr::Kind::Variable) addPre(*a->arg, !poisonsOnError(a->fn));
      std::vector<tuples::ColumnDefAttr> preDefs;
      if (!preExprs.empty()) input = buildExprMap(input, preExprs, scope, "pre", preDefs);

      auto refOfDef = [&](const tuples::ColumnDefAttr& d) { return colMgr.createRef(d.getColumnPtr().get()); };
      auto argRefOf = [&](const sparql::AggregateExpr& a) -> tuples::ColumnRefAttr {
         if (a.arg->kind() == sparql::Expr::Kind::Variable) return varRef(static_cast<const sparql::VariableExpr&>(*a.arg).name);
         return refOfDef(preDefs[preIdx.at(preKey(*a.arg, !poisonsOnError(a.fn)))]);
      };

      llvm::SmallVector<mlir::Attribute> groupRefs;
      std::map<std::string, tuples::ColumnDefAttr> groupDefs;
      for (const auto& g : query.groupBy) {
         tuples::ColumnDefAttr def = g.expr ? preDefs[preIdx.at(preKey(*g.expr, true))] : varDefs.at(g.var);
         groupDefs[g.var] = def;
         groupRefs.push_back(refOfDef(def));
      }

      auto* block = new mlir::Block;
      mlir::Value streamArg = block->addArgument(tuples::TupleStreamType::get(ctxt), loc);
      block->addArgument(tuples::TupleType::get(ctxt), loc);
      llvm::SmallVector<mlir::Attribute> computedCols;
      std::vector<tuples::ColumnDefAttr> resultDefs;
      std::map<size_t, tuples::ColumnDefAttr> avgCountDefs;
      {
         mlir::OpBuilder::InsertionGuard guard(builder);
         builder.setInsertionPointToStart(block);
         std::map<std::string, mlir::Value> distinctStreams;
         llvm::SmallVector<mlir::Value> results;
         for (size_t i = 0; i < aggs.size(); i++) {
            const auto& a = *aggs[i];
            tuples::ColumnRefAttr argRef;
            if (a.arg) argRef = argRefOf(a);
            mlir::Value rel = streamArg;
            if (a.distinct) {
               std::string distinctKey = a.arg ? std::to_string(reinterpret_cast<uintptr_t>(&argRef.getColumn())) : "*";
               auto it = distinctStreams.find(distinctKey);
               if (it != distinctStreams.end()) {
                  rel = it->second;
               } else {
                  llvm::SmallVector<mlir::Attribute> cols;
                  if (!a.arg) {
                     for (const auto& v : allVars) cols.push_back(varRef(v));
                  } else {
                     cols.push_back(argRef);
                  }
                  rel = builder.create<relalg::ProjectionOp>(loc, relalg::SetSemantic::distinct, streamArg, builder.getArrayAttr(cols));
                  distinctStreams[distinctKey] = rel;
               }
            }
            mlir::Value res;
            switch (a.fn) {
               case sparql::AggFn::Count:
                  if (!a.arg)
                     res = builder.create<relalg::CountRowsOp>(loc, i64Type, rel);
                  else
                     res = builder.create<relalg::AggrFuncOp>(loc, i64Type, relalg::AggrFunc::count, rel, argRef);
                  break;
               case sparql::AggFn::Sum:
                  res = builder.create<relalg::AggrFuncOp>(loc, variantType, relalg::AggrFunc::sum, rel, argRef);
                  break;
               case sparql::AggFn::Avg: {
                  res = builder.create<relalg::AggrFuncOp>(loc, variantType, relalg::AggrFunc::sum, rel, argRef);
                  mlir::Value cnt = builder.create<relalg::AggrFuncOp>(loc, i64Type, relalg::AggrFunc::count, rel, argRef);
                  auto cntDef = colMgr.createDef(scope, "agg" + std::to_string(i) + "_count");
                  cntDef.getColumn().type = i64Type;
                  computedCols.push_back(cntDef);
                  results.push_back(cnt);
                  avgCountDefs[i] = cntDef;
                  break;
               }
               case sparql::AggFn::Min:
                  res = builder.create<relalg::AggrFuncOp>(loc, nullableVariantType, relalg::AggrFunc::min, rel, argRef);
                  break;
               case sparql::AggFn::Max:
                  res = builder.create<relalg::AggrFuncOp>(loc, nullableVariantType, relalg::AggrFunc::max, rel, argRef);
                  break;
               case sparql::AggFn::Sample:
                  res = builder.create<relalg::AggrFuncOp>(loc, nullableVariantType, relalg::AggrFunc::any, rel, argRef);
                  break;
               case sparql::AggFn::GroupConcat: {
                  auto op = builder.create<relalg::AggrFuncOp>(loc, nullableVariantType, relalg::AggrFunc::group_concat, rel, argRef);
                  op->setAttr("separator", builder.getStringAttr(a.separator));
                  res = op;
                  break;
               }
            }
            auto def = colMgr.createDef(scope, "agg" + std::to_string(i));
            def.getColumn().type = res.getType();
            computedCols.push_back(def);
            resultDefs.push_back(def);
            results.push_back(res);
         }
         builder.create<tuples::ReturnOp>(loc, results);
      }
      auto aggrOp = builder.create<relalg::AggregationOp>(loc, tuples::TupleStreamType::get(ctxt), input,
                                                          builder.getArrayAttr(groupRefs), builder.getArrayAttr(computedCols));
      aggrOp.getAggrFunc().push_back(block);
      mlir::Value stream = aggrOp.getResult();

      llvm::SmallVector<size_t> postIdx;
      for (size_t i = 0; i < aggs.size(); i++) {
         auto fn = aggs[i]->fn;
         if (fn == sparql::AggFn::Count || fn == sparql::AggFn::Avg || fn == sparql::AggFn::GroupConcat) postIdx.push_back(i);
      }
      if (!postIdx.empty()) {
         auto* mapBlock = new mlir::Block;
         auto tupleArg = mapBlock->addArgument(tuples::TupleType::get(ctxt), loc);
         llvm::SmallVector<mlir::Attribute> mapCols;
         {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(mapBlock);
            llvm::SmallVector<mlir::Value> mapped;
            for (size_t i : postIdx) {
               auto ref = colMgr.createRef(resultDefs[i].getColumnPtr().get());
               if (aggs[i]->fn == sparql::AggFn::Count) {
                  mlir::Value cnt = builder.create<tuples::GetColumnOp>(loc, i64Type, ref, tupleArg);
                  mapped.push_back(builder.create<variant::CreateScalarOp>(loc, variantType, cnt));
               } else if (aggs[i]->fn == sparql::AggFn::GroupConcat) {
                  mlir::Value cat = builder.create<tuples::GetColumnOp>(loc, nullableVariantType, ref, tupleArg);
                  mlir::Value isNull = builder.create<db::IsNullOp>(loc, cat);
                  mlir::Value raw = builder.create<db::NullableGetVal>(loc, cat);
                  mlir::Value emptyStr = builder.create<db::ConstantOp>(loc, db::StringType::get(ctxt), builder.getStringAttr(""));
                  mlir::Value empty = builder.create<variant::CreateScalarOp>(loc, variantType, emptyStr);
                  mapped.push_back(builder.create<mlir::arith::SelectOp>(loc, isNull, empty, raw));
               } else {
                  mlir::Value sum = builder.create<tuples::GetColumnOp>(loc, variantType, ref, tupleArg);
                  auto cntRef = colMgr.createRef(avgCountDefs.at(i).getColumnPtr().get());
                  mlir::Value cnt = builder.create<tuples::GetColumnOp>(loc, i64Type, cntRef, tupleArg);
                  mlir::Value zero = builder.create<mlir::arith::ConstantIntOp>(loc, 0, 64);
                  mlir::Value one = builder.create<mlir::arith::ConstantIntOp>(loc, 1, 64);
                  mlir::Value isEmpty = builder.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, cnt, zero);
                  mlir::Value divisor = builder.create<mlir::arith::SelectOp>(loc, isEmpty, one, cnt);
                  mlir::Value divisorTerm = builder.create<variant::CreateScalarOp>(loc, variantType, divisor);
                  mapped.push_back(builder.create<variant::ArithOp>(loc, variantType, variant::VariantArithPredicate::div, sum, divisorTerm));
               }
               auto def = colMgr.createDef(scope, "agg" + std::to_string(i) + "_term");
               def.getColumn().type = variantType;
               mapCols.push_back(def);
               resultDefs[i] = def;
            }
            builder.create<tuples::ReturnOp>(loc, mapped);
         }
         auto mapOp = builder.create<relalg::MapOp>(loc, tuples::TupleStreamType::get(ctxt), stream, builder.getArrayAttr(mapCols));
         mapOp.getPredicate().push_back(mapBlock);
         stream = mapOp.getResult();
      }

      for (size_t i = 0; i < aggs.size(); i++) aggCols[aggs[i]->key()] = resultDefs[i];

      std::map<std::string, tuples::ColumnDefAttr> newDefs;
      std::vector<std::string> newVars;
      for (const auto& g : query.groupBy) {
         newDefs[g.var] = groupDefs.at(g.var);
         newVars.push_back(g.var);
      }
      for (const auto& se : query.selectExprs) {
         if (se.expr->kind() != sparql::Expr::Kind::Aggregate) continue;
         if (newDefs.count(se.alias))
            throw std::runtime_error("SELECT alias ?" + se.alias + " is already a GROUP BY key");
         newDefs[se.alias] = aggCols.at(static_cast<const sparql::AggregateExpr&>(*se.expr).key());
         newVars.push_back(se.alias);
      }
      varDefs = std::move(newDefs);
      allVars = std::move(newVars);

      if (!query.having.empty()) {
         auto selOp = builder.create<relalg::SelectionOp>(loc, tuples::TupleStreamType::get(ctxt), stream);
         auto* predBlock = new mlir::Block;
         auto tupleArg = predBlock->addArgument(tuples::TupleType::get(ctxt), loc);
         selOp.getPredicate().push_back(predBlock);
         {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(predBlock);
            llvm::SmallVector<mlir::Value> preds;
            for (const auto& h : query.having) preds.push_back(translateFilterExpr(*h, tupleArg));
            mlir::Value pred = preds.size() == 1 ? preds.front() : builder.create<db::AndOp>(loc, preds).getResult();
            builder.create<tuples::ReturnOp>(loc, pred);
         }
         stream = selOp.getResult();
      }
      return stream;
   }

   mlir::Value buildSelectExprs(const sparql::Query& query, mlir::Value stream) {
      for (const auto& se : query.selectExprs) {
         if (query.hasAggregation() && se.expr->kind() == sparql::Expr::Kind::Aggregate) continue; // already bound to its aggregate column
         if (varDefs.count(se.alias))
            throw std::runtime_error("SELECT alias ?" + se.alias + " is already bound");
         std::vector<tuples::ColumnDefAttr> defs;
         stream = buildExprMap(stream, {{se.expr.get(), false}}, colMgr.getUniqueScope("select"), se.alias + "_", defs);
         varDefs[se.alias] = defs.front();
         allVars.push_back(se.alias);
      }
      return stream;
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

         if (query.hasAggregation())
            prevStream = buildAggregation(query, prevStream);
         prevStream = buildSelectExprs(query, prevStream);

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
