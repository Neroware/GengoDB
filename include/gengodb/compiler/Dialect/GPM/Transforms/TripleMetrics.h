#ifndef GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_TRIPLEMETRICS_H
#define GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_TRIPLEMETRICS_H

#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include <algorithm>

namespace gengodb::compiler::dialect::gpm {

double estimateTripleMatches(gpm::TriplePatternOp triple, const gengodb::catalog::GraphMetadataProvider& meta) {
   const gengodb::catalog::TripleStatistics* stats = &meta.getTotalStatistics();
   if (auto predicate = mlir::dyn_cast<gpm::IdentifierTermAttr>(triple.getP())) {
      auto predicateId = meta.resolvePredicate(predicate.getIdent().getValue());
      stats = predicateId ? meta.getPredicateStatistics(*predicateId) : nullptr;
      // predicate does not occur, triple never matches
      if (!stats) return 1.0;
   }
   double count = static_cast<double>(stats->getNumTriples());
   double matches = count;
   if (mlir::isa<gpm::IdentifierTermAttr>(triple.getS())) {
      matches /= std::clamp(static_cast<double>(stats->getNumDistinctSubjects().value_or(count)), 1.0, std::max(count, 1.0));
   }
   if (mlir::isa<gpm::IdentifierTermAttr>(triple.getO())) {
      matches /= std::clamp(static_cast<double>(stats->getNumDistinctObjects().value_or(count)), 1.0, std::max(count, 1.0));
   }
   return std::max(matches, 1.0);
}

} // end namespace lingodb::compiler::dialect

#endif //GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_TRIPLEMETRICS_H
