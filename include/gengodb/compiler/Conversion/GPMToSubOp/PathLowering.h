#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_PATHLOWERING_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_PATHLOWERING_H

#include "gengodb/compiler/Conversion/GPMToSubOp/TripleEmitter.h"

#include "mlir/Transforms/DialectConversion.h"

namespace gengodb::compiler::dialect::gpm::lowering {
// Lowers gpm.relalg.path (a property path leaf) to the closure over its step predicates, computed by a
// semi-naive multi-source BFS on subop.loop. The leaf reads only its own named graph, whose lowering
// registered the graph in `graphs`.
void populatePathLoweringPatterns(mlir::RewritePatternSet& patterns, mlir::TypeConverter& typeConverter, NamedGraphMapper& graphs);
} // namespace gengodb::compiler::dialect::gpm::lowering

#endif // GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_PATHLOWERING_H
