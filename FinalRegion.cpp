#include "FinalRegion.hpp"
#include "DefUseRegion.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>

FinalRegion::FinalRegion(DefUseCombinedRegion &&r) {
  llvm::errs() << "---Composition---\n";
  r.dump();
  curRegion.st = r.region.st;
  /// output##var ->random unmask
  std::map<std::string, ValueInfo> unmasks;
  for (const auto &inst : r.region.instructions) {
    inst.dump();
    if (inst.op != "^") {
      curRegion.instructions.push_back(inst);
      continue;
    }

    // IMPORTANT NOTE: We may have def+use like `t1=t2^r1`

    // TODO: treat it as a Use of t2 at first.
    // Then it becomes to `t1=t2^t1_all_unmask_r1`
    // Then we add the "unmask" variable into t1's XOR-ed set
    // Finally we treat it as a Def of t1.

    // Def+Use
    if (r.outputs2xored.find(inst.assignTo.name) != r.outputs2xored.end()) {
      if (r.outputs2xored.find(inst.lhs.name) != r.outputs2xored.end()) {
        assert(false && "unimplemented");
      } else if (r.outputs2xored.find(inst.rhs.name) != r.outputs2xored.end()) {
        assert(false && "unimplemented");
      }
    }

    // Def
    if (r.outputs2xored.find(inst.assignTo.name) != r.outputs2xored.end()) {
      auto &vars = r.outputs2xored[inst.assignTo.name];
      // generate mask of all xor-ed vars
      bool is_first_var = true;
      size_t id = 0;
      ValueInfo previous;
      for (const auto &xored : vars) {
        if (is_first_var) {
          previous = xored;
          is_first_var = false;
          continue;
        }
        ValueInfo current(inst.assignTo.name + "_allmask_" +
                              std::to_string(++id),
                          VType::Bool, VProp::MASKED, nullptr);
        curRegion.st[current.name] = current;
        curRegion.instructions.push_back(
            Instruction{"^", current, previous, xored});
        previous = current;
      }
      auto &all_mask = previous;
      // then, unmask of each random variables from the "all" mask

      if (id > 0) {
        for (const auto &xored : vars) {
          ValueInfo unmask(inst.assignTo.name + "_unmask_" + xored.name,
                           VType::Bool, VProp::MASKED, nullptr);
          curRegion.st[unmask.name] = unmask;
          curRegion.instructions.push_back(
              Instruction{"^", unmask, all_mask, xored});
          unmasks[inst.assignTo.name + "##" + xored.name] = unmask;
          llvm::errs() << "xored=" << inst.assignTo.name + "##" + xored.name
                       << "\n";
        }

        // finally, insert the def instruction with the random variable replaced
        if (inst.rhs.prop == VProp::RND) {
          curRegion.instructions.push_back(
              Instruction{inst.op, inst.assignTo, inst.lhs,
                          unmasks[inst.assignTo.name + "##" + inst.rhs.name]});
        } else if (inst.lhs.prop == VProp::RND) {
          curRegion.instructions.push_back(Instruction{
              inst.op, inst.assignTo,
              unmasks[inst.assignTo.name + "##" + inst.lhs.name], inst.rhs});
        } else { // FIXME: default to be the right hand side?
          // llvm::errs() << inst.toString() << "\n";
          // llvm::errs() << int(inst.lhs.prop) << " " << int(inst.rhs.prop) <<
          // "\n"; assert(false && "?No random variable in this definition");
          curRegion.instructions.push_back(
              Instruction{inst.op, inst.assignTo, inst.lhs,
                          unmasks[inst.assignTo.name + "##" + inst.rhs.name]});
        }
      } else {
        // FIXME: only xor-ed with a single var... we need a new random variable
        // Or maybe we are done. No more instructions.
        curRegion.instructions.push_back(inst);
      }
      continue;
    }
    // Use: replace. e.g. a=b^r1 => a=b^b_unmask_r1

    if (r.outputs2xored.find(inst.lhs.name) != r.outputs2xored.end()) {
      llvm::errs() << "R=" << inst.rhs.name << "L=" << inst.lhs.name
                   << " Rafter="
                   << unmasks[inst.lhs.name + "##" + inst.rhs.name].name
                   << "\n";
      curRegion.instructions.push_back(
          Instruction{inst.op, inst.assignTo, inst.lhs,
                      unmasks[inst.lhs.name + "##" + inst.rhs.name]});
      continue;
    }
    if (r.outputs2xored.find(inst.rhs.name) != r.outputs2xored.end()) {
      // RHS is the output var, LHS is the random var
      llvm::errs() << "L=" << inst.rhs.name << "R=" << inst.lhs.name << "\n"
                   << " Lafter="
                   << unmasks[inst.rhs.name + "##" + inst.lhs.name].name
                   << "\n";
      curRegion.instructions.push_back(
          Instruction{inst.op, inst.assignTo,
                      unmasks[inst.rhs.name + "##" + inst.lhs.name], inst.rhs});
      continue;
    }

    // Default:
    curRegion.instructions.push_back(inst);
  }
}

void FinalRegion::printAsCode(std::string_view func_name,
                              std::string_view return_name,
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
    llvm::outs() << "bool " << vname << "=0"; // TODO: remove the default value
    llvm::errs() << "origin:" << vname << "\n";
  }
  for (const auto &var : curRegion.st) {
    const auto &vi = var.second;
    if (std::find(original_fparams.begin(), original_fparams.end(), vi.name) !=
        original_fparams.end()) {
      continue;
    }
    // Find all params declared in the function signature
    if ((vi.clangDecl && vi.clangDecl->getKind() == clang::Decl::ParmVar) ||
        vi.prop == VProp::RND) {
      llvm::errs() << "added:" << vi.name << "\n";
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