#ifndef GENGODB_RUNTIME_XSDRUNTIME_H
#define GENGODB_RUNTIME_XSDRUNTIME_H

#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {
using VarLen32 = lingodb::runtime::VarLen32;

// XSD/SPARQL value-semantics comparisons over RDF terms, using by `gsubop.xsd_compare`.
struct XsdRuntime {
   static int8_t compareNodeNode(PropertyGraph::NodeEntry* lhs, PropertyGraph::NodeEntry* rhs, int32_t predicate);
   static int8_t compareNodeLiteral(PropertyGraph::NodeEntry* lhs, VarLen32 rhsLexicalForm, int32_t rhsXsdType, int32_t predicate);
};

} // end namespace lingodb::runtime

#endif // GENGODB_RUNTIME_XSDRUNTIME_H
