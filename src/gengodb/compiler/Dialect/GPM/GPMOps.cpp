#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.h"

#include "gengodb/compiler/Dialect/GPM/IR/GPMDialect.h"

#include "lingodb/compiler/Dialect/TupleStream/TupleStreamOps.h"

#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OpImplementation.h"

#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace lingodb::compiler::dialect;
using namespace gengodb::compiler::dialect;

namespace {
using namespace lingodb::compiler::dialect;
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
        if (mlir::isa<UnitAttr>(a)) {
            attributes.push_back(a);
            continue;
        }
        SymbolRefAttr parsedSymbolRefAttr = mlir::dyn_cast<SymbolRefAttr>(a);
        if (!parsedSymbolRefAttr) return failure();
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
        if (auto parsedSymbolRefAttr = mlir::dyn_cast<tuples::ColumnRefAttr>(a)) {
            p << parsedSymbolRefAttr.getName();
        } else {
            p << a;
        }
    }
    p << "]";
}
ParseResult parseBinding(OpAsmParser& parser, mlir::Attribute& result) {
    SymbolRefAttr attrSymbolAttr;
    if (parser.parseAttribute(attrSymbolAttr, parser.getBuilder().getType<::mlir::NoneType>())) { return failure(); }
    std::string attrName(attrSymbolAttr.getLeafReference().getValue());
    if (parser.parseOptionalLParen().succeeded()) {
        DictionaryAttr dictAttr;
        if (parser.parseAttribute(dictAttr)) { return failure(); }
        mlir::ArrayAttr fromExisting;
        if (parser.parseRParen()) { return failure(); }
        if (parser.parseOptionalEqual().succeeded()) {
            if (parseCustRefArr(parser, fromExisting)) {
                return failure();
            }
        }
        auto attr = getColumnManager(parser).createDef(attrSymbolAttr, fromExisting);
        auto propType = mlir::dyn_cast<TypeAttr>(dictAttr.get("type")).getValue();
        attr.getColumn().type = propType;
        result = attr;
        return success();
    }
    result = getColumnManager(parser).createRef(attrSymbolAttr);
    return success();
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
    parser.getContext()->getOrLoadDialect<tuples::TupleStreamDialect>();
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
ParseResult parseCustAttrMapping(OpAsmParser& parser, ArrayAttr& res) {
    if (parser.parseKeyword("mapping") || parser.parseColon() || parser.parseLBrace()) return failure();
    std::vector<mlir::Attribute> mapping;
    while (true) {
        if (!parser.parseOptionalRBrace()) { break; }
        tuples::ColumnDefAttr attrDefAttr;
        if (parseCustDef(parser, attrDefAttr)) {
            return failure();
        }
        mapping.push_back(attrDefAttr);
        if (!parser.parseOptionalComma()) { continue; }
        if (parser.parseRBrace()) { return failure(); }
        break;
    }
    res = mlir::ArrayAttr::get(parser.getBuilder().getContext(), mapping);
    return success();
}
void printCustAttrMapping(OpAsmPrinter& p, mlir::Operation* op, Attribute mapping) {
    p << " mapping: {";
    auto first = true;
    for (auto attr : mlir::cast<ArrayAttr>(mapping)) {
        if (first) {
            first = false;
        } else {
            p << ", ";
        }
        printCustDef(p, op, mlir::cast<tuples::ColumnDefAttr>(attr));
    }
    p << "}";
}
ParseResult parseTerm(OpAsmParser& parser, mlir::Attribute& attr) {
    auto ctxt = parser.getContext();
    if (!parser.parseOptionalKeyword("id")) {
        std::string ident;
        if (parser.parseLBrace() || parser.parseString(&ident) || parser.parseRBrace()) {
            return failure();
        }
        attr = gpm::IdentifierTermAttr::get(ctxt, StringAttr::get(ctxt, ident));
        return success();
    }
    else if (!parser.parseOptionalKeyword("_")) {
        std::string localId;
        if (parser.parseLBrace() || parser.parseString(&localId) || parser.parseRBrace()) {
            return failure();
        }
        attr = gpm::BNodeTermAttr::get(ctxt, StringAttr::get(ctxt, localId));
        return success();
    }
    else if (!parser.parseOptionalQuestion()) {
        if (parser.parseLBrace()) {
            return failure();
        }
        mlir::Attribute binding;
        if (parseBinding(parser, binding)) {
            return failure();
        }
        if (binding && parser.parseRBrace().succeeded()) { 
            attr = gpm::VariableTermAttr::get(ctxt, binding);
            return success();
        }
    }
    return failure();
}
void printTerm(OpAsmPrinter& p, mlir::Operation* op, mlir::Attribute attr) {
    llvm::TypeSwitch<::mlir::Attribute>(attr)
        .Case<gpm::IdentifierTermAttr>([&](auto idTerm) {
            idTerm.print(p);
        })
        .Case<gpm::BNodeTermAttr>([&](auto bnode) {
            bnode.print(p);
        })
        .Case<gpm::VariableTermAttr>([&](auto varTerm) {
            p << "?{";
            if (varTerm.hasBinding()) {
                printCustRef(p, op, varTerm.getBindingReference());
            }
            else {
                printCustDef(p, op, varTerm.getProducedBinding());
            }
            p << "}";
        });
}
ParseResult parseTripleBindings(OpAsmParser& parser, DictionaryAttr& result) {
    SmallVector<NamedAttribute> entries;
    auto parseEntry = [&]() -> ParseResult {
        std::string position;
        tuples::ColumnDefAttr def;
        if (parser.parseKeywordOrString(&position) || parser.parseColon() || parseCustDef(parser, def)) { return failure(); }
        entries.emplace_back(StringAttr::get(parser.getContext(), position), def);
        return success();
    };
    if (parser.parseCommaSeparatedList(parseEntry)) { return failure(); }
    result = DictionaryAttr::get(parser.getContext(), entries);
    return success();
}
void printTripleBindings(OpAsmPrinter& p, mlir::Operation* op, DictionaryAttr bindings) {
    SmallVector<NamedAttribute> entries;
    for (auto position : {"s", "p", "o"}) {
        if (auto entry = bindings.getNamed(position)) entries.push_back(*entry);
    }
    llvm::interleaveComma(entries, p, [&](NamedAttribute entry) {
        p << entry.getName().getValue() << ": ";
        printCustDef(p, op, mlir::cast<tuples::ColumnDefAttr>(entry.getValue()));
    });
}
ParseResult parseTripleBNodes(OpAsmParser& parser, DictionaryAttr& result) {
    SmallVector<NamedAttribute> entries;
    auto parseEntry = [&]() -> ParseResult {
        std::string localId;
        mlir::Attribute column;
        if (parser.parseKeywordOrString(&localId) || parser.parseColon() || parseBinding(parser, column)) { return failure(); }
        entries.emplace_back(StringAttr::get(parser.getContext(), localId), column);
        return success();
    };
    if (parser.parseCommaSeparatedList(parseEntry)) { return failure(); }
    result = DictionaryAttr::get(parser.getContext(), entries);
    return success();
}
void printTripleBNodes(OpAsmPrinter& p, mlir::Operation* op, DictionaryAttr bnodeScope) {
    llvm::interleaveComma(bnodeScope, p, [&](NamedAttribute entry) {
        p.printKeywordOrString(entry.getName().getValue());
        p << ": ";
        if (auto def = mlir::dyn_cast<tuples::ColumnDefAttr>(entry.getValue())) {
            printCustDef(p, op, def);
        }
        else {
            printCustRef(p, op, mlir::cast<tuples::ColumnRefAttr>(entry.getValue()));
        }
    });
}
ParseResult parseCustRegion(OpAsmParser& parser, Region& result) {
    OpAsmParser::Argument predArgument;
    SmallVector<OpAsmParser::Argument, 4> regionArgs;
    SmallVector<Type, 4> argTypes;
    if (parser.parseLParen()) {
        return failure();
    }
    while (true) {
        Type predArgType;
        if (!parser.parseOptionalRParen()) {
            break;
        }
        if (parser.parseArgument(predArgument) || parser.parseColonType(predArgType)) {
            return failure();
        }
        predArgument.type = predArgType;
        regionArgs.push_back(predArgument);
        if (!parser.parseOptionalComma()) { continue; }
        if (parser.parseRParen()) { return failure(); }
        break;
    }

    if (parser.parseRegion(result, regionArgs)) return failure();
    return success();
}
void printCustRegion(OpAsmPrinter& p, Operation* op, Region& r) {
    p << "(";
    bool first = true;
    for (auto arg : r.front().getArguments()) {
        if (first) {
            first = false;
        } else {
            p << ",";
        }
        p << arg << ": " << arg.getType();
    }
    p << ")";
    p.printRegion(r, false, true);
}
} // namespace

::mlir::LogicalResult gpm::TriplePatternOp::verify() {
    auto isValidTerm = [](mlir::Attribute attr) {
        return mlir::isa<
            IdentifierTermAttr,
            BNodeTermAttr,
            VariableTermAttr
        >(attr);
    };
    if (!isValidTerm(getS())) {
        return emitOpError("subject must be a GPM term attribute");
    }
    if (!isValidTerm(getP())) {
        return emitOpError("predicate must be a GPM term attribute");
    } 
    if (!isValidTerm(getO())) {
        return emitOpError("object must be a GPM term attribute");
    }
    if (mlir::isa<BNodeTermAttr>(getP())) {
        return emitOpError("predicate cannot be a blank node");
    }
    std::pair<llvm::StringRef, mlir::Attribute> positions[] = {{"s", getS()}, {"p", getP()}, {"o", getO()}};
    if (auto bindings = getBindingsAttr()) {
        for (auto entry : bindings) {
            auto* position = llvm::find_if(positions, [&](auto& pos) { return pos.first == entry.getName().getValue(); });
            if (position == std::end(positions)) {
                return emitOpError("binding key must be one of 's', 'p' or 'o', got '") << entry.getName().getValue() << "'";
            }
            if (!mlir::isa<VariableTermAttr, BNodeTermAttr>(position->second)) {
                return emitOpError("binding for '") << position->first << "' requires a variable or blank node term";
            }
            if (!mlir::isa<tuples::ColumnDefAttr>(entry.getValue())) {
                return emitOpError("binding for '") << position->first << "' must be a column definition";
            }
        }
    }
    if (auto bnodeScope = getBnodeScopeAttr()) {
        for (auto entry : bnodeScope) {
            bool used = llvm::any_of(positions, [&](auto& pos) {
                auto bnode = mlir::dyn_cast<BNodeTermAttr>(pos.second);
                return bnode && bnode.getLocalId().getValue() == entry.getName().getValue();
            });
            if (!used) {
                return emitOpError("bnode entry '") << entry.getName().getValue() << "' does not correspond to a blank node of this pattern";
            }
            if (!mlir::isa<tuples::ColumnDefAttr, tuples::ColumnRefAttr>(entry.getValue())) {
                return emitOpError("bnode entry '") << entry.getName().getValue() << "' must be a column definition or reference";
            }
        }
    }
    return mlir::success();
}
::mlir::LogicalResult gpm::BasicGraphPatternOp::verify() {
    return gpm::detail::verifyGraphPatternBody(getOperation());
}
::mlir::LogicalResult gpm::OptionalGraphPatternOp::verify() {
    return gpm::detail::verifyGraphPatternBody(getOperation());
}
::mlir::LogicalResult gpm::BagOp::verify() {
    return gpm::detail::verifyUnionMapping(getOperation(), getMapping());
}

#define GET_OP_CLASSES
#include "gengodb/compiler/Dialect/GPM/IR/GPMOps.cpp.inc"