#ifndef GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_PATHEXPANSIONLOWERING_H
#define GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_PATHEXPANSIONLOWERING_H

#include "gengodb/compiler/Conversion/GPMToSubOp/TripleEmitter.h"
#include "lingodb/compiler/Dialect/RelAlg/ColumnSet.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Transforms/DialectConversion.h"

namespace gengodb::compiler::dialect::gpm::lowering {
// Relalg columns of each gpm.relalg.path_expansion's input. The lowering materializes its input
// into a buffer, so it needs the complete column set; it is collected before the conversion starts
// (a lowered subop stream no longer reports its columns). Pure analysis: column types are final once
// gpm-unnest-patterns has run, so nothing here changes the IR. Also fixes the type of each
// op's fresh target column (variant), so that it is final before the first conversion round.
using PathInputColumns = llvm::DenseMap<mlir::Operation*, lingodb::compiler::dialect::relalg::ColumnSet>;
PathInputColumns collectPathInputColumns(mlir::ModuleOp module);

// Lowers gpm.relalg.path_expansion to a semi-naive multi-source BFS over subop.loop.
void populatePathExpansionLoweringPatterns(mlir::RewritePatternSet& patterns, mlir::TypeConverter& typeConverter, NamedGraphMapper& graphs, ExternalGraphMapper& externalGraphs, PathInputColumns& inputColumns);
} // namespace gengodb::compiler::dialect::gpm::lowering

#endif // GENGODB_COMPILER_CONVERSION_GPMTOSUBOP_PATHEXPANSIONLOWERING_H
