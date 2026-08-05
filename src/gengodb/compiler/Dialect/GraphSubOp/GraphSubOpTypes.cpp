#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpDialect.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOps.h"
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.h"
#include "lingodb/compiler/Dialect/SubOperator/SubOperatorDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include <llvm/ADT/TypeSwitch.h>

using namespace mlir;
using namespace gengodb::compiler::dialect::gsubop;
namespace {
   using namespace lingodb::compiler::dialect;
   using namespace gengodb::compiler::dialect;
   static mlir::LogicalResult parseStateMembers(mlir::AsmParser& parser, subop::StateMembersAttr& stateMembersAttr) {
      auto& memberManager = parser.getContext()->getOrLoadDialect<subop::SubOperatorDialect>()->getMemberManager();
      if (parser.parseLSquare()) return mlir::failure();
      llvm::SmallVector<subop::Member> members;
      while (true) {
         if (!parser.parseOptionalRSquare()) { break; }
         llvm::StringRef colName;
         mlir::Type t;
         if (parser.parseKeyword(&colName) || parser.parseColon() || parser.parseType(t)) { return mlir::failure(); }
         members.push_back(memberManager.createMemberDirect(colName.str(), t));
         if (!parser.parseOptionalComma()) { continue; }
         if (parser.parseRSquare()) { return mlir::failure(); }
         break;
      }
      stateMembersAttr = subop::StateMembersAttr::get(parser.getContext(), members);
      return mlir::success();
   }
   static void printStateMembers(mlir::AsmPrinter& p, subop::StateMembersAttr stateMembersAttr) {
      auto& memberManager = stateMembersAttr.getContext()->getOrLoadDialect<subop::SubOperatorDialect>()->getMemberManager();
      p << "[";
      auto first = true;
      for (subop::Member m : stateMembersAttr.getMembers()) {
         if (first) {
            first = false;
         } else {
            p << ", ";
         }
         p << memberManager.getName(m) << " : " << memberManager.getType(m);
      }
      p << "]";
   }
} // namespace

subop::StateMembersAttr gsubop::GraphType::getMembers() {
   llvm::SmallVector<subop::Member> combined;
   combined.reserve(getNodeMembers().getMembers().size() + getEdgeMembers().getMembers().size());
   for (const auto& member : getNodeMembers().getMembers()) {
      combined.push_back(member);
   }
   for (const auto& member : getEdgeMembers().getMembers()) {
      combined.push_back(member);
   }
   return subop::StateMembersAttr::get(this->getContext(), combined);
}
subop::StateMembersAttr gsubop::NodeRefType::getMembers() {
   llvm::SmallVector<subop::Member> combined;
   combined.reserve(getNodeMembers().getMembers().size() 
      + getIncomingMembers().getMembers().size()
      + getOutgoingMembers().getMembers().size());
   for (const auto& member : getNodeMembers().getMembers()) {
      combined.push_back(member);
   }
   for (const auto& member : getIncomingMembers().getMembers()) {
      combined.push_back(member);
   }
   for (const auto& member : getOutgoingMembers().getMembers()) {
      combined.push_back(member);
   }
   return subop::StateMembersAttr::get(this->getContext(), combined);
}
subop::StateMembersAttr gsubop::EdgeRefType::getMembers() {
   llvm::SmallVector<subop::Member> combined;
   combined.reserve(getEdgeMembers().getMembers().size() 
      + getFromMembers().getMembers().size()
      + getToMembers().getMembers().size()
      + getPropertyMembers().getMembers().size());
   for (const auto& member : getEdgeMembers().getMembers()) {
      combined.push_back(member);
   }
   for (const auto& member : getFromMembers().getMembers()) {
      combined.push_back(member);
   }
   for (const auto& member : getFromMembers().getMembers()) {
      combined.push_back(member);
   }
   for (const auto& member : getPropertyMembers().getMembers()) {
      combined.push_back(member);
   }
   return subop::StateMembersAttr::get(this->getContext(), combined);
}

#define GET_TYPEDEF_CLASSES
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.cpp.inc"
void gengodb::compiler::dialect::gsubop::GraphSubOpDialect::registerTypes() {
   addTypes<
#define GET_TYPEDEF_LIST
#include "gengodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypes.cpp.inc"

      >();
}
// #include "lingodb/compiler/Dialect/GraphSubOp/GraphSubOpsTypeInterfaces.cpp.inc"