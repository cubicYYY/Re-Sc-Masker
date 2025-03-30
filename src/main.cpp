#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm-16/llvm/Support/raw_ostream.h>

#include <cassert>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "DataStructures.hpp"
#include "DefUseRegion.hpp"
#include "FinalRegion.hpp"
#include "MaskedRegion.hpp"
#include "RegionDivider.hpp"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "config.hpp"

using namespace clang::tooling;
using namespace llvm;

static llvm::cl::OptionCategory toolCategory("Re-SC-Masker <options>");

// TODO: move inside of the class
Region globalRegion;
std::vector<std::string> original_fparams;
ValueInfo ret_var;

class ScMaskerASTVisitor : public clang::RecursiveASTVisitor<ScMaskerASTVisitor> {
 public:
  int depth = 0;  // Tracks the current depth in the AST

  bool VisitDecl(clang::Decl *decl) {
    auto ctxt = decl->getDeclContext();
    if (!ctxt) {
      return true;
    }

    // Check if the declaration is inside a function body
    if (auto *funcDecl = clang::dyn_cast<clang::FunctionDecl>(ctxt)) {
      printIndented("Decl", decl);
      // We are inside a function, so visit the declaration
      std::string declType = decl->getDeclKindName();

      // Capture the function param declarations and insert it into the symbol
      // table
      if (auto *varDecl = clang::dyn_cast<clang::VarDecl>(decl)) {
        VProp prop = VProp::UNK;
        auto varName = varDecl->getNameAsString();

        if (varDecl->getKind() == clang::Decl::ParmVar) {
          // Check if it's a pointer type (output parameter)
          if (varDecl->getType()->isPointerType()) {
            prop = VProp::OUTPUT;
          }
          // Otherwise check the naming convention
          else if (varName[0] == 'r') {
            prop = VProp::RND;
          } else if (varName[0] == 'k') {
            prop = VProp::SECRET;
          } else {
            prop = VProp::PUB;
          }
          original_fparams.push_back(varName);
        } else {
          prop = VProp::UNK;
        }
        // Determine width from variable name
        auto type = varDecl->getType();
        llvm::errs() << "type=" << type.getAsString() << "\n";
        int width = getWidthFromType(type.getAsString());  // default width

        auto vi = ValueInfo(varName, width, prop, varDecl);
        globalRegion.st[varName] = vi;

        llvm::errs() << "ST inserted:" << varName << " " << toString(prop) << "\n";
        varDecl->dump();
      }
    }
    return true;  // Continue the traversal
  }

  clang::Stmt *unfold(clang::Stmt *expr) {
    // If the expression is an ImplicitCastExpr, we need to unwrap the cast and
    // get the actual operand.
    llvm::errs() << "Unwrapping...\n";
    expr->dump();
    if (auto *castExpr = clang::dyn_cast<clang::ImplicitCastExpr>(expr)) {
      expr = castExpr->getSubExpr();
      return unfold(expr);
    }

    // If the expression is an ParenExpr, the same.
    if (auto *parenExpr = clang::dyn_cast<clang::ParenExpr>(expr)) {
      expr = parenExpr->getSubExpr();
      return unfold(expr);
    }
    return expr;  // Return the original expression if it's not a cast
  }

  bool VisitStmt(clang::Stmt *stmt) {
    if (stmt) {
      printIndented("Stmt", stmt);
    }
    // Extract assignment operators
    // Check for assignments (BinaryOperator with BO_Assign)
    if (auto *binOp = clang::dyn_cast<clang::BinaryOperator>(stmt)) {
      if (binOp->getOpcode() == clang::BinaryOperatorKind::BO_Assign) {  // "assignment"
        clang::Expr *wrappedAssignTo = binOp->getLHS();
        clang::Expr *wrappedAssignWith = binOp->getRHS();
        // wrappedAssignTo->children();
        assert(wrappedAssignTo != nullptr);
        assert(wrappedAssignWith != nullptr);

        // // Extract actual operands, unwrapping implicit casts if needed
        auto assign_to = unfold(wrappedAssignTo);
        auto assignWith = unfold(wrappedAssignWith);
        assert(assign_to != nullptr);
        assert(assignWith != nullptr);
        llvm::errs() << "....";
        assign_to->dump();
        llvm::errs() << "====";
        assignWith->dump();

        auto *assign_toRef = clang::dyn_cast<clang::DeclRefExpr>(assign_to);
        if (assign_toRef) {
          llvm::errs() << assign_toRef->getNameInfo().getAsString() << "\n";
        } else {
          llvm::errs() << "NO! " << assign_toRef->getNameInfo().getAsString() << "\n";
          return false;  // FAILED
        }
        // Get the LHS variable
        std::string lhsStr;
        llvm::raw_string_ostream lhsOS(lhsStr);
        assign_to->printPretty(lhsOS, nullptr, clang::PrintingPolicy(clang::LangOptions()));

        // Check the expression
        if (auto *nestedBinOp = clang::dyn_cast<clang::BinaryOperator>(assignWith)) {
          // BINOP: C = A op B
          llvm::errs() << "-----BINOP Assignment: \n";
          // TODO: allow constants
          auto oprand1 = clang::dyn_cast<clang::DeclRefExpr>(unfold(nestedBinOp->getLHS()));
          auto oprand2 = clang::dyn_cast<clang::DeclRefExpr>(unfold(nestedBinOp->getRHS()));
          assert(oprand1);
          assert(oprand2);
          oprand1->dump();
          oprand2->dump();
          llvm::errs() << "-----BINOP end\n";
          globalRegion.instructions.push_back(
              Instruction(clang::BinaryOperator::getOpcodeStr(nestedBinOp->getOpcode()).str(),
                          globalRegion.st[assign_toRef->getDecl()->getNameAsString()],
                          globalRegion.st[oprand1->getDecl()->getNameAsString()],
                          globalRegion.st[oprand2->getDecl()->getNameAsString()]));
        } else if (auto *unOp = clang::dyn_cast<clang::UnaryOperator>(assignWith)) {
          // UOP: C = op A
          llvm::errs() << "-----UOP Assignment: \n";
          // TODO: allow constants
          auto oprand = clang::dyn_cast<clang::DeclRefExpr>(unfold(unOp->getSubExpr()));
          assert(oprand);
          oprand->dump();
          llvm::errs() << "-----UOP end\n";
          globalRegion.instructions.push_back(Instruction(clang::UnaryOperator::getOpcodeStr(unOp->getOpcode()).str(),
                                                          globalRegion.st[assign_toRef->getDecl()->getNameAsString()],
                                                          globalRegion.st[oprand->getDecl()->getNameAsString()],
                                                          ValueInfo()));
        } else if (auto *directRef = clang::dyn_cast<clang::DeclRefExpr>(assignWith)) {
          // Handle direct assignments (a = b)
          llvm::errs() << "-----Direct Assignment: \n";
          directRef->dump();
          llvm::errs() << assign_toRef->getDecl()->getNameAsString() << "\n";
          llvm::errs() << directRef->getDecl()->getNameAsString() << "\n";
          llvm::errs() << "-----Direct Assignment end\n";
          globalRegion.instructions.push_back(
              Instruction("=",  // Use assignment operator
                          globalRegion.st[assign_toRef->getDecl()->getNameAsString()],
                          globalRegion.st[directRef->getDecl()->getNameAsString()],
                          ValueInfo()));  // No third operand needed for direct assignment
        } else {
          // Invalid: Other forms
          std::string rhsStr;
          llvm::raw_string_ostream rhsOS(rhsStr);
          assignWith->printPretty(rhsOS, nullptr, clang::PrintingPolicy(clang::LangOptions()));
        }
        return true;  // done with this statement
      }
      return true;
    }
    if (auto *retStmt = clang::dyn_cast<clang::ReturnStmt>(stmt)) {
      auto ret = unfold(retStmt->getRetValue());
      llvm::errs() << "-----RET\n";
      ret->dump();
      assert(clang::dyn_cast<clang::DeclRefExpr>(ret));
      auto retVarName = clang::dyn_cast<clang::DeclRefExpr>(ret)->getDecl()->getNameAsString();
      ret_var = ValueInfo(
          retVarName, getWidthFromType(clang::dyn_cast<clang::DeclRefExpr>(ret)->getDecl()->getType().getAsString()),
          VProp::OUTPUT, nullptr);
      return true;
    }
    return true;
  }
  bool TraverseDecl(clang::Decl *decl) {
    depth++;  // Entering a deeper level
    bool result = clang::RecursiveASTVisitor<ScMaskerASTVisitor>::TraverseDecl(decl);
    // llvm::errs() << "Call trav. decl.\n";
    depth--;  // Returning to the parent level
    return result;
  }

  bool TraverseStmt(clang::Stmt *stmt) {
    depth++;  // Entering a deeper level
    bool result = clang::RecursiveASTVisitor<ScMaskerASTVisitor>::TraverseStmt(stmt);
    // llvm::errs() << "Call trav. stmt.\n";

    depth--;  // Returning to the parent level
    return result;
  }

 private:
  // Helper to print a node with indentation based on depth
  void printIndented(const char *type, const clang::Stmt *stmt) {
    llvm::errs().indent(depth * 2) << type << " (" << stmt->getStmtClassName() << "): ";
    stmt->printPretty(llvm::errs(), nullptr, clang::PrintingPolicy(clang::LangOptions()));
    llvm::errs() << "\n";
    stmt->dump();  // Print raw declaration details
  }

  void printIndented(const char *type, const clang::Decl *decl) {
    llvm::errs().indent(depth * 2) << type << " (" << decl->getDeclKindName() << "): ";
    decl->print(llvm::errs(), clang::PrintingPolicy(clang::LangOptions()));
    llvm::errs() << "\n";
    decl->dump();  // Print raw declaration details
  }
};

// The "actual" main function is here
class ScMaskerASTConsumer : public clang::ASTConsumer {
 public:
  ScMaskerASTConsumer(clang::CompilerInstance &ci, llvm::StringRef file) {}
  void HandleTranslationUnit(clang::ASTContext &context) override {
    clang::TranslationUnitDecl *tuDecl = context.getTranslationUnitDecl();
    ScMaskerASTVisitor visitor;
    visitor.TraverseDecl(tuDecl);

    llvm::errs() << "---DUMP---\n";
    globalRegion.dump();

    // Bit-blasting

#ifdef SCM_Z3_BLASTING_ENABLED
    // TODO: apply bit-blasting to sub-regions but not the global one!
    llvm::errs() << "---Bit-Blast(Global)---\n";
    // FIXME: get the correct ValueInfo of the returned value!
    auto blasted = BitBlastPass(globalRegion.st, ret_var);
    blasted.add(std::move(globalRegion));
    globalRegion = blasted.get();
    llvm::errs() << "Blasted insts: " << globalRegion.count() << "\n";
#endif

    // Replace each region with a masked region
    llvm::errs() << "---REPLACE---\n";
    TrivialRegionDivider divider(globalRegion);

    DefUseRegion<TrivialMaskedRegion> combinator;
    // combinator.region.st = globalRegion.st;

    // Output Xor Set
    llvm::errs() << "---OUTPUT Xor Set---\n";
    for (const auto &[var, xorset] : combinator.output2xors) {
      llvm::errs() << var << ": ";
      for (const auto &xorvar : xorset) {
        llvm::errs() << xorvar << " ";
      }
      llvm::errs() << "\n";
    }

    auto region_id = 0;
    while (!divider.done()) {
      Region subRegion = divider.next();
      TrivialMaskedRegion masked(subRegion);
      llvm::errs() << "Masked: " + std::to_string(region_id++) + "\n";
      masked.dump();
      combinator.add(std::move(masked));
    }
    llvm::errs() << "\n===DefUse Combined===\n";
    combinator.dump();

    llvm::errs() << "---COMPOSITE---\n";
    // pass the original arguments to make the order unchanged
    FinalRegion{std::move(combinator)}.printAsCode("masked_func", ret_var, original_fparams);
  }
};

class ScMaskerFrontendAction : public clang::ASTFrontendAction {
 protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef file) override {
    return std::make_unique<ScMaskerASTConsumer>(ci, file);
  }
};

class ScMaskerFrontendActionFactory : public clang::tooling::FrontendActionFactory {
 public:
  ScMaskerFrontendActionFactory() {}
  std::unique_ptr<clang::FrontendAction> create() override { return std::make_unique<ScMaskerFrontendAction>(); }
};

void divideAndPrintRegion(const Region &region) {
  TrivialRegionDivider divider(region);

  llvm::errs() << "Dividing and printing regions:\n";
  while (!divider.done()) {
    Region subRegion = divider.next();
    if (!subRegion.isEnd()) {
      subRegion.dump();  // Dump the sub-region
    }
  }
}

void init() {}

int main(int argc, const char **argv) {
  init();
  auto argsParser = CommonOptionsParser::create(argc, argv, toolCategory);

  CommonOptionsParser &optionsParser = argsParser.get();
  ClangTool tool(optionsParser.getCompilations(), optionsParser.getSourcePathList());
  auto af = std::make_unique<ScMaskerFrontendActionFactory>();
  auto result = tool.run(af.get());
}