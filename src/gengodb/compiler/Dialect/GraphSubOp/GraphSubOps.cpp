#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"

#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"

#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OpImplementation.h"

#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;

namespace {
tuples::ColumnManager& getColumnManager(::mlir::OpAsmParser& parser) {
   return parser.getBuilder().getContext()->getLoadedDialect<tuples::TupleStreamDialect>()->getColumnManager();
}
ParseResult parseCustRef(OpAsmParser& parser, tuples::ColumnRefAttr& attr) {
   ::mlir::SymbolRefAttr parsedSymbolRefAttr;
   if (parser.parseAttribute(parsedSymbolRefAttr, parser.getBuilder().getType<::mlir::NoneType>())) { return failure(); }
   attr = getColumnManager(parser).createRef(parsedSymbolRefAttr);
   return success();
}
void printCustRef(OpAsmPrinter& p, mlir::Operation* op, tuples::ColumnRefAttr attr) {
   p << attr.getName();
}
ParseResult parseCustRefArr(OpAsmParser& parser, ArrayAttr& attr) {
   ArrayAttr parsedAttr;
   std::vector<Attribute> attributes;
   if (parser.parseAttribute(parsedAttr, parser.getBuilder().getType<::mlir::NoneType>())) {
      return failure();
   }
   for (auto a : parsedAttr) {
      SymbolRefAttr parsedSymbolRefAttr = mlir::dyn_cast<SymbolRefAttr>(a);
      tuples::ColumnRefAttr attr = getColumnManager(parser).createRef(parsedSymbolRefAttr);
      attributes.push_back(attr);
   }
   attr = ArrayAttr::get(parser.getBuilder().getContext(), attributes);
   return success();
}

void printCustRefArr(OpAsmPrinter& p, mlir::Operation* op, ArrayAttr arrayAttr) {
   p << "[";
   std::vector<Attribute> attributes;
   bool first = true;
   for (auto a : arrayAttr) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      tuples::ColumnRefAttr parsedSymbolRefAttr = mlir::dyn_cast<tuples::ColumnRefAttr>(a);
      p << parsedSymbolRefAttr.getName();
   }
   p << "]";
}
ParseResult parseCustDef(OpAsmParser& parser, tuples::ColumnDefAttr& attr) {
   SymbolRefAttr attrSymbolAttr;
   if (parser.parseAttribute(attrSymbolAttr, parser.getBuilder().getType<::mlir::NoneType>())) { return failure(); }
   std::string attrName(attrSymbolAttr.getLeafReference().getValue());
   if (parser.parseLParen()) { return failure(); }
   DictionaryAttr dictAttr;
   if (parser.parseAttribute(dictAttr)) { return failure(); }
   mlir::ArrayAttr fromExisting;
   if (parser.parseRParen()) { return failure(); }
   if (parser.parseOptionalEqual().succeeded()) {
      if (parseCustRefArr(parser, fromExisting)) {
         return failure();
      }
   }
   attr = getColumnManager(parser).createDef(attrSymbolAttr, fromExisting);
   auto propType = mlir::dyn_cast<TypeAttr>(dictAttr.get("type")).getValue();
   attr.getColumn().type = propType;
   return success();
}
void printCustDef(OpAsmPrinter& p, mlir::Operation* op, tuples::ColumnDefAttr attr) {
   p << attr.getName();
   std::vector<mlir::NamedAttribute> relAttrDefProps;
   MLIRContext* context = attr.getContext();
   const tuples::Column& relationalAttribute = attr.getColumn();
   relAttrDefProps.push_back({mlir::StringAttr::get(context, "type"), mlir::TypeAttr::get(relationalAttribute.type)});
   p << "(" << mlir::DictionaryAttr::get(context, relAttrDefProps) << ")";
   Attribute fromExisting = attr.getFromExisting();
   if (fromExisting) {
      ArrayAttr fromExistingArr = mlir::dyn_cast_or_null<ArrayAttr>(fromExisting);
      p << "=";
      printCustRefArr(p, op, fromExistingArr);
   }
}

void mapResults(mlir::IRMapping& mapping, mlir::Operation* from, mlir::Operation* to) {
   for (auto i = 0ul; i < from->getNumResults(); i++) {
      mapping.map(from->getResult(i), to->getResult(i));
   }
}

} // namespace 

void gsubop::ScanGraphOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getGraph() && newType != state.getType()) {
      auto newNodeSetType = transformer.getNewRefType(this->getOperation(), getNodeSet().getColumn().type);
      auto newEdgeSetType = transformer.getNewRefType(this->getOperation(), getEdgeSet().getColumn().type);
      setNodeSetAttr(transformer.createReplacementColumn(getNodeSetAttr(), newNodeSetType));
      setEdgeSetAttr(transformer.createReplacementColumn(getEdgeSetAttr(), newEdgeSetType));
   }
}
void gsubop::ScanGraphOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::ScanGraphOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<ScanGraphOp>(this->getLoc(), mapping.lookupOrDefault(getGraph()), columnMapping.clone(getNodeSet()), columnMapping.clone(getEdgeSet()));
   mapResults(mapping, this->getOperation(), newOp.getOperation());
   
   return newOp;
}

void gsubop::ScanNodeSetOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getNodeSet() && newType != state.getType()) {
      auto newRefType = transformer.getNewRefType(this->getOperation(), getRef().getColumn().type);
      setRefAttr(transformer.createReplacementColumn(getRefAttr(), newRefType));
   }
}
void gsubop::ScanNodeSetOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::ScanNodeSetOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<ScanNodeSetOp>(this->getLoc(), mapping.lookupOrDefault(getNodeSet()), columnMapping.clone(getRef()));
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

void gsubop::ScanEdgeSetOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getEdgeSet() && newType != state.getType()) {
      auto newRefType = transformer.getNewRefType(this->getOperation(), getRef().getColumn().type);
      setRefAttr(transformer.createReplacementColumn(getRefAttr(), newRefType));
   }
}
void gsubop::ScanEdgeSetOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::ScanEdgeSetOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<ScanEdgeSetOp>(this->getLoc(), mapping.lookupOrDefault(getEdgeSet()), columnMapping.clone(getRef()));
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

void gsubop::ScanPropertySetOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getPropSet() && newType != state.getType()) {
      auto newRefType = transformer.getNewRefType(this->getOperation(), getRef().getColumn().type);
      setRefAttr(transformer.createReplacementColumn(getRefAttr(), newRefType));
   }
}
void gsubop::ScanPropertySetOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::ScanPropertySetOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<ScanPropertySetOp>(this->getLoc(), mapping.lookupOrDefault(getPropSet()), columnMapping.clone(getRef()));
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

void gsubop::NodeCountOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getGraph() && newType != state.getType()) {
      auto newRefType = transformer.getNewRefType(this->getOperation(), getRef().getColumn().type);
      setRefAttr(transformer.createReplacementColumn(getRefAttr(), newRefType));
   }
}
void gsubop::NodeCountOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::NodeCountOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<NodeCountOp>(this->getLoc(), mapping.lookupOrDefault(getStream()), getGraph(), getRef());
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

void gsubop::EdgeCountOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getGraph() && newType != state.getType()) {
      auto newRefType = transformer.getNewRefType(this->getOperation(), getRef().getColumn().type);
      setRefAttr(transformer.createReplacementColumn(getRefAttr(), newRefType));
   }
}
void gsubop::EdgeCountOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::EdgeCountOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<EdgeCountOp>(this->getLoc(), mapping.lookupOrDefault(getStream()), getGraph(), getRef());
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

void gsubop::NodeDegreeOp::updateStateType(subop::SubOpStateUsageTransformer& transformer, mlir::Value state, mlir::Type newType) {
   if (state == getGraph() && newType != state.getType()) {
      auto newRefType = transformer.getNewRefType(this->getOperation(), getRef().getColumn().type);
      setRefAttr(transformer.createReplacementColumn(getRefAttr(), newRefType));
   }
}
void gsubop::NodeDegreeOp::replaceColumns(subop::SubOpStateUsageTransformer& transformer, tuples::Column* oldColumn, tuples::Column* newColumn) {
   assert(false && "should not happen");
}
mlir::Operation* gsubop::NodeDegreeOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<NodeDegreeOp>(this->getLoc(), mapping.lookupOrDefault(getStream()), getGraph(), getRef(), getDir());
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

mlir::Operation* gsubop::FilterByIdentifierOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<FilterByIdentifierOp>(this->getLoc(), mapping.lookupOrDefault(getStream()), getRef(), getIdent());
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

mlir::Operation* gsubop::CastPropertyRefOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<CastPropertyRefOp>(this->getLoc(), mapping.lookupOrDefault(getStream()), getRef(), getTypedRef());
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}

mlir::Operation* gsubop::GraphRefToStringOp::cloneSubOp(mlir::OpBuilder& builder, mlir::IRMapping& mapping, subop::ColumnMapping& columnMapping) {
   auto newOp = builder.create<GraphRefToStringOp>(this->getLoc(), mapping.lookupOrDefault(getStream()), getRef(), getStrRef());
   mapResults(mapping, this->getOperation(), newOp.getOperation());

   return newOp;
}


#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.cpp.inc"