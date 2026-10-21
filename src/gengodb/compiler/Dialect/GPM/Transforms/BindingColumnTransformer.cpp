#include "gengodb/compiler/Dialect/GPM/Transforms/BindingColumnTransformer.h"

#include "lingodb/compiler/Dialect/RelAlg/IR/RelAlgOpsAttributes.h"
#include "gengodb/compiler/Dialect/GPM/IR/GPMOpsAttributes.h"

namespace gengodb::compiler::dialect::gpm {

mlir::Attribute BindingColumnTransformer::remapColumnAttr(mlir::Attribute attr, const ColumnMapper& colMap) {
    if (!attr) return attr;
    if (auto varTerm = mlir::dyn_cast<gpm::VariableTermAttr>(attr)) {
        auto binding = varTerm.getBinding();
        auto newBinding = remapColumnAttr(binding, colMap);
        if (newBinding == binding) return attr;
        return gpm::VariableTermAttr::get(attr.getContext(), newBinding);
    }
    if (auto colRef = mlir::dyn_cast<tuples::ColumnRefAttr>(attr)) {
        auto it = colMap.find(&colRef.getColumn());
        if (it != colMap.end()) return columnManager.createRef(it->second);
        return attr;
    }
    if (auto colDef = mlir::dyn_cast<tuples::ColumnDefAttr>(attr)) {
        auto fromExisting = colDef.getFromExisting();
        if (!fromExisting) return attr;
        auto newFromExisting = remapColumnAttr(fromExisting, colMap);
        if (newFromExisting == fromExisting) return attr;
        return columnManager.createDef(&colDef.getColumn(), newFromExisting);
    }
    if (auto sortSpec = mlir::dyn_cast<relalg::SortSpecificationAttr>(attr)) {
        auto newRef = remapColumnAttr(sortSpec.getAttr(), colMap);
        if (newRef == sortSpec.getAttr()) return attr;
        return relalg::SortSpecificationAttr::get(attr.getContext(), mlir::cast<tuples::ColumnRefAttr>(newRef), sortSpec.getSortSpec());
    }
    if (auto arr = mlir::dyn_cast<mlir::ArrayAttr>(attr)) {
        bool changed = false;
        llvm::SmallVector<mlir::Attribute> newElems;
        newElems.reserve(arr.size());
        for (auto e : arr) {
            auto ne = remapColumnAttr(e, colMap);
            changed |= ne != e;
            newElems.push_back(ne);
        }
        return changed ? mlir::ArrayAttr::get(arr.getContext(), newElems) : attr;
    }
    if (auto dict = mlir::dyn_cast<mlir::DictionaryAttr>(attr)) {
        bool changed = false;
        llvm::SmallVector<mlir::NamedAttribute> newElems;
        for (auto na : dict) {
            auto ne = remapColumnAttr(na.getValue(), colMap);
            changed |= ne != na.getValue();
            newElems.emplace_back(na.getName(), ne);
        }
        return changed ? mlir::DictionaryAttr::get(dict.getContext(), newElems) : attr;
    }
    return attr;
}
void BindingColumnTransformer::remapColumnsOf(mlir::Operation* op, const ColumnMapper& colMap) {
    bool changed = false;
    llvm::SmallVector<mlir::NamedAttribute> newAttrs;
    for (auto namedAttr : op->getAttrDictionary()) {
        auto remapped = remapColumnAttr(namedAttr.getValue(), colMap);
        changed |= remapped != namedAttr.getValue();
        newAttrs.emplace_back(namedAttr.getName(), remapped);
    }
    if (changed) op->setAttrs(mlir::DictionaryAttr::get(op->getContext(), newAttrs));
}
void BindingColumnTransformer::remapColumnsIn(const llvm::SmallPtrSetImpl<mlir::Operation*>& ops, const ColumnMapper& colMap) {
    if (colMap.empty()) return;
    for (auto* op : ops) remapColumnsOf(op, colMap);
}
void BindingColumnTransformer::remapColumnsEverywhere(mlir::Operation* op, mlir::Operation* skip, const ColumnMapper& colMap, const llvm::SmallPtrSetImpl<mlir::Operation*>* exclude) {
    if (colMap.empty()) return;
    op->walk([&](mlir::Operation* op) {
        if (op == skip) return;
        if (exclude && exclude->contains(op)) return;
        remapColumnsOf(op, colMap);
    });
}

} // end namespace gengodb::compiler::dialect::gpm