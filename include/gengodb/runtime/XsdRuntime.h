#ifndef GENGODB_RUNTIME_XSDRUNTIME_H
#define GENGODB_RUNTIME_XSDRUNTIME_H

#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {
using VarLen32 = lingodb::runtime::VarLen32;

// XSD/SPARQL value-semantics comparisons over RDF terms, using by `gsubop.xsd_compare`.
struct XsdRuntime {
   static int8_t compareNodeNode(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate);
   static int8_t compareNodeLiteral(PropertyGraph::NodeEntry* lhs, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate);
   static VarLen32 literalLexicalOfRef(PropertyGraph::NodeEntry* ref);
   static int32_t literalTypeOfRef(PropertyGraph::NodeEntry* ref);
   static VarLen32 arithLexical(VarLen32 lhsLexicalForm, int32_t lhsXsdType, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate);
   static int32_t arithType(VarLen32 lhsLexicalForm, int32_t lhsXsdType, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate);
   static VarLen32 negateLexical(VarLen32 lexicalForm, int32_t xsdType);
   static int32_t negateType(VarLen32 lexicalForm, int32_t xsdType);
   static int8_t compareDyn(VarLen32 lhsLexicalForm, int32_t lhsXsdType, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate);
};

} // end namespace lingodb::runtime

#endif // GENGODB_RUNTIME_XSDRUNTIME_H
