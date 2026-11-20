#ifndef GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_GRAPHMETRICS_H
#define GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_GRAPHMETRICS_H

#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include <algorithm>

namespace gengodb::compiler::dialect::gpm {

inline const gengodb::catalog::GraphMetadataProvider* getGraphMetadata(gpm::TriplePatternOp triple) {
   if (auto namedGraphOp = triple.getRel().getDefiningOp<gpm::NamedGraphOp>()) {
      if (auto meta = mlir::dyn_cast_or_null<gpm::GraphMetaDataAttr>(namedGraphOp->getAttr("meta"))) {
         return meta.getMeta().get();
      }
   }
   return nullptr;
}
inline const gengodb::catalog::TripleStatistics* getTripleStatistics(gpm::TriplePatternOp triple, const gengodb::catalog::GraphMetadataProvider& meta) {
   if (auto predicate = mlir::dyn_cast<gpm::IdentifierTermAttr>(triple.getP())) {
      auto predicateId = meta.resolvePredicate(predicate.getIdent().getValue());
      return predicateId ? meta.getPredicateStatistics(*predicateId) : nullptr;
   }
   return &meta.getTotalStatistics();
}

namespace metrics {

inline double estimateTripleMatches(gpm::TriplePatternOp triple, const gengodb::catalog::GraphMetadataProvider& meta) {
   const auto* stats = getTripleStatistics(triple, meta);
   // predicate does not occur, triple never matches
   if (!stats) return 1.0;
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
inline std::optional<double> estimateTripleDistinctValues(gpm::TriplePatternOp triple, const gengodb::catalog::GraphMetadataProvider& meta, const lingodb::compiler::dialect::tuples::Column* column) {
   auto produces = [&](mlir::Attribute term) {
      auto variable = mlir::dyn_cast<gpm::VariableTermAttr>(term);
      if (!variable) return false;
      auto def = variable.getProducedBinding();
      return def && &def.getColumn() == column;
   };
   const auto* stats = getTripleStatistics(triple, meta);
   if (!stats) return std::nullopt;
   double count = static_cast<double>(stats->getNumTriples());
   std::optional<size_t> distinct;
   if (produces(triple.getS())) {
      distinct = stats->getNumDistinctSubjects();
   } else if (produces(triple.getO())) {
      distinct = stats->getNumDistinctObjects();
   } else if (produces(triple.getP())) {
      distinct = meta.getPredicates().size();
   } else {
      return std::nullopt;
   }
   if (!distinct) return std::nullopt;
   return std::clamp(static_cast<double>(*distinct), 1.0, std::max(count, 1.0));
}

} // end namespace metrics
} // end namespace gengodb::compiler::dialect::gpm

#endif //GENGODB_COMPILER_DIALECT_GPM_IR_TRANSFORMS_GRAPHMETRICS_H
