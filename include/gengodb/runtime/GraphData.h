#ifndef GENGODB_RUNTIME_GRAPHDATA_H
#define GENGODB_RUNTIME_GRAPHDATA_H

#include "gengodb/runtime/BuiltinGraphs.h"

namespace lingodb::runtime {

struct GraphData {
    static uint8_t* allocAndPopulateBuiltinGraph(int32_t builtin);
    static SimpleGraph* allocSimpleGraphState(size_t nodeBufLen, size_t relBufLen);
    static PropertyGraph* allocPropertyGraphState(size_t nodeBufLen, size_t relBufLen, size_t propBufLen);
    static void createGraph(lingodb::runtime::VarLen32 meta);
    static PropertyGraph* getGraph(lingodb::runtime::VarLen32 name, lingodb::runtime::VarLen32 iri);
}; // GraphHelper

} // namespace lingodb::runtime

#endif // GENGODB_RUNTIME_GRAPHDATA_H