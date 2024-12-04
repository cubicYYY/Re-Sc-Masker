#pragma once

#include "DataStructures.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>
#include <string_view>

class CombinedRegion {
public:
  CombinedRegion() {}
  // Change type to TrivialMaskedRegion, but there is cyclic independence
  void add(Region &&newRegion) {
    newRegion.dump(); // Dump the sub-region
    // Extend
    llvm::errs() << "---EXTEND---\n";
    curRegion.dump();
    llvm::errs() << "-------new:\n";
    newRegion.dump();
    llvm::errs() << "-------start:\n";
    for (auto &this_inst : newRegion.instructions) {
      if (curRegion.outputs2xored.find(this_inst.lhs.name) !=
          curRegion.outputs2xored.end()) {
        llvm::errs() << "found used:" << this_inst.lhs.name << "\n";
        // FIXME:
        // assert(this_inst.rhs.prop == VProp::MASKED ||
        //  this_inst.rhs.prop == VProp::RND);
        auto &rnd_to_swap = this_inst.rhs;
        // The left part is a previous output var: swap the right side
        // FIXME: faster search! we should locate it in O(1)
        // Find the corresponding inst. to swap with
        for (auto &ori_inst : curRegion.instructions) {
          llvm::errs() << ori_inst.toString() << "\n";
          if (ori_inst.assignTo != this_inst.lhs) {
            continue;
          }
          if (ori_inst.lhs == curRegion.outputs2xored[this_inst.lhs.name]) {
            std::swap(rnd_to_swap, ori_inst.lhs);
            break;
          }
          if (ori_inst.rhs == curRegion.outputs2xored[this_inst.lhs.name]) {
            std::swap(rnd_to_swap, ori_inst.rhs);
            break;
          }
        }
        curRegion.outputs2xored.erase(this_inst.lhs.name);
      } else if (curRegion.outputs2xored.find(this_inst.rhs.name) !=
                 curRegion.outputs2xored.end()) {
        // FIXME:
        // assert(this_inst.lhs.prop == VProp::MASKED ||
        //        this_inst.lhs.prop == VProp::RND);
        auto &rnd_to_swap = this_inst.lhs;
        // The right part is a previous output var: swap the left side
        // FIXME: faster search! we should locate it in O(1)
        // Find the corresponding inst. to swap with
        for (auto &ori_inst : curRegion.instructions) {
          if (ori_inst.assignTo != this_inst.rhs) {
            continue;
          }
          if (ori_inst.lhs == curRegion.outputs2xored[this_inst.rhs.name]) {
            std::swap(rnd_to_swap, ori_inst.lhs);
            break;
          }
          if (ori_inst.rhs == curRegion.outputs2xored[this_inst.rhs.name]) {
            std::swap(rnd_to_swap, ori_inst.rhs);
            break;
          }
        }
      }
    }
    curRegion.instructions.insert(curRegion.instructions.end(),
                                  newRegion.instructions.begin(),
                                  newRegion.instructions.end());
    curRegion.outputs2xored.insert(newRegion.outputs2xored.begin(),
                                   newRegion.outputs2xored.end());
    auto sizeBefore = curRegion.st.size();
    curRegion.st.insert(newRegion.st.begin(), newRegion.st.end());
    llvm::errs() << "Size: +" << curRegion.st.size() - sizeBefore << "\n";
  }
  void printAsCode(std::string_view func_name, std::string_view return_name,
                   std::vector<std::string> original_fparams) const {
    std::vector<ValueInfo> tempVars;
    llvm::errs() << "=====GOT RESULT=====" << "\n";
    // func decl
    llvm::outs() << "bool " << func_name << "(";
    bool isFirstParam = true;
    for (const auto &vname : original_fparams) {
      // Find all params declared in the function signature
      if (isFirstParam) {
        isFirstParam = false;
      } else {
        llvm::outs() << ",";
      }
      llvm::outs() << "bool " << vname
                   << "=0"; // TODO: remove the default value
    }
    for (const auto &var : curRegion.st) {
      const auto &vi = var.second;
      if (std::find(original_fparams.begin(), original_fparams.end(),
                    vi.name) != original_fparams.end()) {
        continue;
      }
      // Find all params declared in the function signature
      if ((vi.clangDecl && vi.clangDecl->getKind() == clang::Decl::ParmVar) ||
          vi.prop == VProp::RND) {
        if (isFirstParam) {
          isFirstParam = false;
        } else {
          llvm::outs() << ",";
        }
        llvm::outs() << "bool " << vi.name
                     << "=0"; // TODO: remove the default value
      } else {
        tempVars.push_back(vi);
      }
    }
    llvm::outs() << ")";

    llvm::outs() << "{\n";
    // local variable decl
    for (const auto &tempVar : tempVars) {
      llvm::outs() << "bool " << tempVar.name << ";\n";
    }
    // insts
    for (const auto &instruction : curRegion.instructions) {
      llvm::outs() << instruction.toString() << "\n";
    }
    llvm::outs() << "return " << return_name << ";\n";
    llvm::outs() << "}\n";
  }

public:
  Region curRegion;
};
