#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm-16/llvm/Support/raw_ostream.h>

#include <cassert>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "Re-Sc-Masker/BitBlastPass.hpp"
#include "Re-Sc-Masker/Config.hpp"
#include "Re-Sc-Masker/Preludes.hpp"
#include "Re-Sc-Masker/RegionCollector.hpp"
#include "Re-Sc-Masker/RegionConcatenater.hpp"
#include "Re-Sc-Masker/RegionDivider.hpp"
#include "Re-Sc-Masker/RegionMasker.hpp"

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
                    original_fparams.emplace_back(varName);
                } else {
                    prop = VProp::UNK;
                }
                // Determine width from variable name
                auto type = varDecl->getType();
                llvm::errs() << "type=" << type.getAsString() << "\n";
                int width = getWidthFromType(type.getAsString());  // default width

                auto vi = ValueInfo(varName, width, prop, varDecl);
                globalRegion.sym_tbl[varName] = vi;

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
                auto res = unfold(wrappedAssignTo);
                auto assignWith = unfold(wrappedAssignWith);
                assert(res != nullptr);
                assert(assignWith != nullptr);
                llvm::errs() << "....";
                res->dump();
                llvm::errs() << "====";
                assignWith->dump();

                auto *resRef = clang::dyn_cast<clang::DeclRefExpr>(res);
                if (resRef) {
                    llvm::errs() << resRef->getNameInfo().getAsString() << "\n";
                } else {
                    llvm::errs() << "NO! " << resRef->getNameInfo().getAsString() << "\n";
                    return false;  // FAILED
                }
                // Get the LHS variable
                std::string lhsStr;
                llvm::raw_string_ostream lhsOS(lhsStr);
                res->printPretty(lhsOS, nullptr, clang::PrintingPolicy(clang::LangOptions()));

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
                    globalRegion.insts.emplace_back(clang::BinaryOperator::getOpcodeStr(nestedBinOp->getOpcode()).str(),
                                                    globalRegion.sym_tbl[resRef->getDecl()->getNameAsString()],
                                                    globalRegion.sym_tbl[oprand1->getDecl()->getNameAsString()],
                                                    globalRegion.sym_tbl[oprand2->getDecl()->getNameAsString()]);
                } else if (auto *unOp = clang::dyn_cast<clang::UnaryOperator>(assignWith)) {
                    // UOP: C = op A
                    llvm::errs() << "-----UOP Assignment: \n";
                    // TODO: allow constants
                    auto oprand = clang::dyn_cast<clang::DeclRefExpr>(unfold(unOp->getSubExpr()));
                    assert(oprand);
                    oprand->dump();
                    llvm::errs() << "-----UOP end\n";
                    globalRegion.insts.emplace_back(clang::UnaryOperator::getOpcodeStr(unOp->getOpcode()).str(),
                                                    globalRegion.sym_tbl[resRef->getDecl()->getNameAsString()],
                                                    globalRegion.sym_tbl[oprand->getDecl()->getNameAsString()],
                                                    ValueInfo());
                } else if (auto *directRef = clang::dyn_cast<clang::DeclRefExpr>(assignWith)) {
                    // Handle direct assignments (a = b)
                    llvm::errs() << "-----Direct Assignment: \n";
                    directRef->dump();
                    llvm::errs() << resRef->getDecl()->getNameAsString() << "\n";
                    llvm::errs() << directRef->getDecl()->getNameAsString() << "\n";
                    llvm::errs() << "-----Direct Assignment end\n";
                    globalRegion.insts.emplace_back("=",  // Use assignment operator
                                                    globalRegion.sym_tbl[resRef->getDecl()->getNameAsString()],
                                                    globalRegion.sym_tbl[directRef->getDecl()->getNameAsString()],
                                                    ValueInfo());  // No third operand needed for direct assignment
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
                retVarName,
                getWidthFromType(clang::dyn_cast<clang::DeclRefExpr>(ret)->getDecl()->getType().getAsString()),
                VProp::OUTPUT, nullptr);
            return true;
        }
        if (auto *declStmt = clang::dyn_cast<clang::DeclStmt>(stmt)) {
            llvm::errs() << "-----DeclStmt\n";
            declStmt->dump();

            // Process each declaration in the statement
            for (auto decl : declStmt->decls()) {
                if (auto *varDecl = clang::dyn_cast<clang::VarDecl>(decl)) {
                    std::string varName = varDecl->getNameAsString();
                    std::string typeStr = varDecl->getType().getAsString();

                    // Register the variable in the symbol table
                    VProp prop = VProp::UNK;  // Default property for local variables
                    int width = getWidthFromType(typeStr);

                    // Create ValueInfo and add to symbol table
                    globalRegion.sym_tbl[varName] = ValueInfo{varName, width, prop, nullptr};

                    llvm::errs() << "Internal variable: " << varName << " of type: " << typeStr << "\n";
                }
            }
            llvm::errs() << "-----DeclStmt end\n";
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
        clang::TranslationUnitDecl *TUDecl = context.getTranslationUnitDecl();
        ScMaskerASTVisitor visitor;

        // Parse the original program
        visitor.TraverseDecl(TUDecl);

        llvm::errs() << "---Global Region DUMP---\n";
        globalRegion.dump();

        // Bit-blasting
        llvm::errs() << "---Bit-Blast(Per Instr.)---\n";
        auto blasted = Z3BitBlastPass(ret_var, std::move(globalRegion));
        globalRegion = blasted.get();
        globalRegion.dump();

        // REPLACE phase: Replace each region with a masked region
        llvm::errs() << "---REPLACE---\n";
        auto global_st = globalRegion.sym_tbl;

        // !!! Main pipeline is here
        auto divided = TrivialRegionDivider(std::move(globalRegion));
        auto masked = TrivialRegionMasker(std::move(divided));
        auto combined = RegionCollector(std::move(masked));
        auto final = RegionConcatenater(std::move(combined));

        final.printAsCode("masked_func", ret_var, original_fparams);
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

void init() {}

int main(int argc, const char **argv) {
    init();
    auto argsParser = CommonOptionsParser::create(argc, argv, toolCategory);

    CommonOptionsParser &optionsParser = argsParser.get();
    ClangTool tool(optionsParser.getCompilations(), optionsParser.getSourcePathList());
    auto af = std::make_unique<ScMaskerFrontendActionFactory>();
    auto result = tool.run(af.get());
}