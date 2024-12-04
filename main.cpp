
#include "CombinedRegion.hpp"
#include "DataStructures.hpp"
#include "MaskedRegion.hpp"
#include "RegionDivider.hpp"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include <cassert>
#include <clang/AST/RecursiveASTVisitor.h>

#include <llvm-16/llvm/Support/raw_ostream.h>
#include <memory>
#include <vector>

using namespace clang::tooling;
using namespace llvm;

static llvm::cl::OptionCategory toolCategory("Re-SC-Masker <options>");

// TODO: move inside of the class
Region globalRegion;
std::vector<std::string> original_fparams;

class ScMaskerASTVisitor
    : public clang::RecursiveASTVisitor<ScMaskerASTVisitor> {
public:
  int depth = 0; // Tracks the current depth in the AST

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

        if (varDecl->getKind() ==
            clang::Decl::ParmVar) { // FIXME: do not judge the type by the name
          if (varName[0] == 'r') {
            prop = VProp::RND;
          } else if (varName[0] == 'k') {
            prop = VProp::SECRET;
          } else {
            prop = VProp::PUB;
          }
        } else {
          // label all intermediate var as unknown (i.e. not sure if it is
          // masked)
          prop = VProp::UNK;
        }
        // insert into the ST
        auto vi = ValueInfo(varName, VType::Bool, prop, varDecl);
        globalRegion.st[varName] = vi;
        // record the original param order
        original_fparams.push_back(vi.name);
        llvm::errs() << "ST inserted:" << varName << " " << toString(prop)
                     << "\n";
        varDecl->dump();
      }
    }
    return true; // Continue the traversal
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
    return expr; // Return the original expression if it's not a cast
  }

  bool VisitStmt(clang::Stmt *stmt) {
    if (stmt) {
      printIndented("Stmt", stmt);
    }
    // Extract assignment operators
    // Check for assignments (BinaryOperator with BO_Assign)
    if (auto *binOp = clang::dyn_cast<clang::BinaryOperator>(stmt)) {
      if (binOp->getOpcode() ==
          clang::BinaryOperatorKind::BO_Assign) { // "assignment"
        clang::Expr *wrappedAssignTo = binOp->getLHS();
        clang::Expr *wrappedAssignWith = binOp->getRHS();
        // wrappedAssignTo->children();
        assert(wrappedAssignTo != nullptr);
        assert(wrappedAssignWith != nullptr);

        // // Extract actual operands, unwrapping implicit casts if needed
        auto assignTo = unfold(wrappedAssignTo);
        auto assignWith = unfold(wrappedAssignWith);
        assert(assignTo != nullptr);
        assert(assignWith != nullptr);
        llvm::errs() << "....";
        assignTo->dump();
        llvm::errs() << "====";
        assignWith->dump();

        auto *assignToRef = clang::dyn_cast<clang::DeclRefExpr>(assignTo);
        if (assignToRef) {
          llvm::errs() << assignToRef->getNameInfo().getAsString() << "\n";
        } else {
          llvm::errs() << "NO! " << assignToRef->getNameInfo().getAsString()
                       << "\n";
          return false; // FAILED
        }
        // Get the LHS variable
        std::string lhsStr;
        llvm::raw_string_ostream lhsOS(lhsStr);
        assignTo->printPretty(lhsOS, nullptr,
                              clang::PrintingPolicy(clang::LangOptions()));

        // Check the expression
        if (auto *nestedBinOp =
                clang::dyn_cast<clang::BinaryOperator>(assignWith)) {
          // BINOP: C = A op B
          llvm::errs() << "-----BINOP Assignment: \n";
          // TODO: allow constants
          auto oprand1 = clang::dyn_cast<clang::DeclRefExpr>(
              unfold(nestedBinOp->getLHS()));
          auto oprand2 = clang::dyn_cast<clang::DeclRefExpr>(
              unfold(nestedBinOp->getRHS()));
          assert(oprand1);
          assert(oprand2);
          oprand1->dump();
          oprand2->dump();
          llvm::errs() << "-----BINOP end\n";
          globalRegion.append(Instruction(
              clang::BinaryOperator::getOpcodeStr(nestedBinOp->getOpcode())
                  .str(),
              globalRegion.st[assignToRef->getDecl()->getNameAsString()],
              globalRegion.st[oprand1->getDecl()->getNameAsString()],
              globalRegion.st[oprand2->getDecl()->getNameAsString()]));
        } else if (auto *unOp =
                       clang::dyn_cast<clang::UnaryOperator>(assignWith)) {
          // UOP: C = op A
          llvm::errs() << "-----UOP Assignment: \n";
          // TODO: allow constants
          auto oprand =
              clang::dyn_cast<clang::DeclRefExpr>(unfold(unOp->getSubExpr()));
          assert(oprand);
          oprand->dump();
          llvm::errs() << "-----UOP end\n";
          globalRegion.append(Instruction(
              clang::UnaryOperator::getOpcodeStr(unOp->getOpcode()).str(),
              globalRegion.st[assignToRef->getDecl()->getNameAsString()],
              globalRegion.st[oprand->getDecl()->getNameAsString()],
              ValueInfo()));
        } else {
          // Invalid: Other forms
          std::string rhsStr;
          llvm::raw_string_ostream rhsOS(rhsStr);
          assignWith->printPretty(rhsOS, nullptr,
                                  clang::PrintingPolicy(clang::LangOptions()));
        }
        return true; // done with this statement
      }
      return true;
    }
    if (auto *retStmt = clang::dyn_cast<clang::ReturnStmt>(stmt)) {
      auto ret = unfold(retStmt->getRetValue());
      llvm::errs() << "-----RET\n";
      ret->dump();
      assert(clang::dyn_cast<clang::DeclRefExpr>(ret));
      auto retVarName = clang::dyn_cast<clang::DeclRefExpr>(ret)
                            ->getDecl()
                            ->getNameAsString();
      globalRegion.outputs2xored[retVarName] = {};
      return true;
    }
    return true;
  }
  bool TraverseDecl(clang::Decl *decl) {
    depth++; // Entering a deeper level
    bool result =
        clang::RecursiveASTVisitor<ScMaskerASTVisitor>::TraverseDecl(decl);
    // llvm::errs() << "Call trav. decl.\n";
    depth--; // Returning to the parent level
    return result;
  }

  bool TraverseStmt(clang::Stmt *stmt) {

    depth++; // Entering a deeper level
    bool result =
        clang::RecursiveASTVisitor<ScMaskerASTVisitor>::TraverseStmt(stmt);
    // llvm::errs() << "Call trav. stmt.\n";

    depth--; // Returning to the parent level
    return result;
  }

private:
  // Helper to print a node with indentation based on depth
  void printIndented(const char *type, const clang::Stmt *stmt) {
    llvm::errs().indent(depth * 2)
        << type << " (" << stmt->getStmtClassName() << "): ";
    stmt->printPretty(llvm::errs(), nullptr,
                      clang::PrintingPolicy(clang::LangOptions()));
    llvm::errs() << "\n";
    stmt->dump(); // Print raw declaration details
  }

  void printIndented(const char *type, const clang::Decl *decl) {
    llvm::errs().indent(depth * 2)
        << type << " (" << decl->getDeclKindName() << "): ";
    decl->print(llvm::errs(), clang::PrintingPolicy(clang::LangOptions()));
    llvm::errs() << "\n";
    decl->dump(); // Print raw declaration details
  }
};

class ScMaskerASTConsumer : public clang::ASTConsumer {
public:
  ScMaskerASTConsumer(clang::CompilerInstance &ci, llvm::StringRef file) {}
  void HandleTranslationUnit(clang::ASTContext &context) override {
    clang::TranslationUnitDecl *tuDecl = context.getTranslationUnitDecl();
    ScMaskerASTVisitor visitor;
    visitor.TraverseDecl(tuDecl);

    llvm::errs() << "---DUMP---\n";
    globalRegion.dump();

    // Replace
    llvm::errs() << "---REPLACE---\n";
    TrivialRegionDivider divider(globalRegion);
    CombinedRegion res;
    res.curRegion.st = globalRegion.st;
    res.curRegion.dump();
    llvm::errs() << "---COMPOSITE---\n";
    while (!divider.done()) {
      Region subRegion = divider.next();
      subRegion.dump();
      TrivialMaskedRegion maskedRegion(subRegion);
      res.add(std::move(maskedRegion.region));
    }
    res.curRegion.dump();
    // pass the original arguments to make the order keep the same
    res.printAsCode("masked_func", globalRegion.outputs2xored.begin()->first,
                    original_fparams);
  }
};

class ScMaskerFrontendAction : public clang::ASTFrontendAction {
protected:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci,
                    llvm::StringRef file) override {
    return std::make_unique<ScMaskerASTConsumer>(ci, file);
  }
};

class ScMaskerFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
public:
  ScMaskerFrontendActionFactory() {}
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<ScMaskerFrontendAction>();
  }
};

void divideAndPrintRegion(const Region &region) {
  TrivialRegionDivider divider(region);

  llvm::errs() << "Dividing and printing regions:\n";
  while (!divider.done()) {
    Region subRegion = divider.next();
    if (!subRegion.isEnd()) {
      subRegion.dump(); // Dump the sub-region
    }
  }
}

int main(int argc, const char **argv) {
  auto argsParser = CommonOptionsParser::create(argc, argv, toolCategory);

  CommonOptionsParser &optionsParser = argsParser.get();
  ClangTool tool(optionsParser.getCompilations(),
                 optionsParser.getSourcePathList());
  auto af = std::make_unique<ScMaskerFrontendActionFactory>();
  auto result = tool.run(af.get());
}