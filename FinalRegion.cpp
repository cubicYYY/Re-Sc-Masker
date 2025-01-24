#include "FinalRegion.hpp"
#include "DataStructures.hpp"
#include "DefUseRegion.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>

FinalRegion::FinalRegion(DefUseCombinedRegion &&r) {
  llvm::errs() << "---Composition---\n";
  r.dump();
  curRegion.st = r.region.st;
  /// output##var ->random unmask
  std::map<std::string, ValueInfo> unmasks;

  // Pre-scan
  llvm::errs() << "--Pre-scanning--\n";
  r.dump();
  llvm::errs() << "outputs2xors:\n";
  for (const auto &xor_set : r.outputs2xors) {
    llvm::errs() << xor_set.first << ": ";
    for (const auto &xored : xor_set.second) {
      llvm::errs() << xored.name << " ";
    }
    llvm::errs() << "\n";
  }
  llvm::errs() << "----------\n";

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

    auto assignTo_ref2 = r.alias2var.count(inst.assignTo.name)
                             ? r.alias2var[inst.assignTo.name]
                             : inst.assignTo.name;
    auto rhs_ref2 = r.alias2var.count(inst.rhs.name)
                        ? r.alias2var[inst.rhs.name]
                        : inst.rhs.name;
    auto lhs_ref2 = r.alias2var.count(inst.lhs.name)
                        ? r.alias2var[inst.lhs.name]
                        : inst.lhs.name;

    // 1. Def+Use: unimplemented
    if (r.outputs2xors.count(assignTo_ref2)) {
      if (r.outputs2xors.count(lhs_ref2)) {
        assert(false && "unimplemented");
      } else if (r.outputs2xors.count(rhs_ref2)) {
        assert(false && "unimplemented");
      }
    }

    // 2. Def, and the var belongs to the output
    if (r.outputs2xors.count(assignTo_ref2)) {

      auto &xor_vars = r.outputs2xors[assignTo_ref2];
      // generate mask of all xor-ed vars
      bool is_first_xor_var = true;
      size_t id = 0;
      ValueInfo previous;
      for (const auto &xored : xor_vars) {
        if (is_first_xor_var) {
          previous = xored;
          is_first_xor_var = false;
          continue;
        }
        ValueInfo allmask(assignTo_ref2 + "_allmask_" + std::to_string(++id), 1,
                          VProp::MASKED, nullptr);
        curRegion.st[allmask.name] = allmask;
        curRegion.instructions.push_back(
            Instruction{"^", allmask, previous, xored});
        previous = allmask;
      }
      auto &all_mask = previous;
      // then, unmask of each random variables from the "all" mask

      if (id > 0) {
        for (const auto &xored : xor_vars) {
          ValueInfo unmask(assignTo_ref2 + "_unmask_" + xored.name, 1,
                           VProp::MASKED, nullptr);
          curRegion.st[unmask.name] = unmask;
          curRegion.instructions.push_back(
              Instruction{"^", unmask, all_mask, xored});
          unmasks[assignTo_ref2 + "##" + xored.name] = unmask;
          llvm::errs() << "xored=" << assignTo_ref2 + "##" + xored.name << "\n";
        }

        // finally, insert the def instruction with the random variable replaced
        if (inst.rhs.prop == VProp::RND) {
          curRegion.instructions.push_back(
              Instruction{inst.op, inst.assignTo, inst.lhs,
                          unmasks[assignTo_ref2 + "##" + inst.rhs.name]});
        } else if (inst.lhs.prop == VProp::RND) {
          curRegion.instructions.push_back(Instruction{
              inst.op, inst.assignTo,
              unmasks[assignTo_ref2 + "##" + inst.lhs.name], inst.rhs});
        } else { // FIXME: default to be the right hand side?
          // llvm::errs() << inst.toString() << "\n";
          // llvm::errs() << int(inst.lhs.prop) << " " << int(inst.rhs.prop) <<
          // "\n"; assert(false && "?No random variable in this definition");
          curRegion.instructions.push_back(
              Instruction{inst.op, inst.assignTo, inst.lhs,
                          unmasks[assignTo_ref2 + "##" + inst.rhs.name]});
        }
      } else {
        // FIXME: only xor-ed with a single var... we need a new random variable
        // Or maybe we are done. No more instructions.
        curRegion.instructions.push_back(inst);
      }
      continue;
    }

    // 3. Use: replace. e.g. a=b^r1 => a=b^b_unmask_r1
    // FIXME: Only for the first use of each variable in each region!
    if (r.outputs2xors.count(lhs_ref2)) { // lhs is the output var: replace the rhs (random var)
      if (!unmasks.count(lhs_ref2 + "##" + inst.rhs.name)) {
        llvm::errs() << "R=" << inst.rhs.name << " L=" << inst.lhs.name
                     << "\n failed\n";
        curRegion.instructions.push_back(inst);          
        continue;
      }
      llvm::errs() << "R=" << inst.rhs.name << " L=" << inst.lhs.name
                   << "\nRafter="
                   << unmasks[lhs_ref2 + "##" + inst.rhs.name].name
                   << "\n";
      curRegion.instructions.push_back(
          Instruction{inst.op, inst.assignTo, inst.lhs,
                      unmasks[lhs_ref2 + "##" + inst.rhs.name]});
      continue;
    }

    if (r.outputs2xors.count(rhs_ref2)) { // rhs is the output var: replace the lhs (random var)
      if (!unmasks.count(lhs_ref2 + "##" + inst.rhs.name)) {
        llvm::errs() << "R=" << inst.rhs.name << " L=" << inst.lhs.name
                     << "\n failed\n";
        curRegion.instructions.push_back(inst);
        continue;
      }
      llvm::errs() << "R=" << inst.rhs.name << "L=" << inst.lhs.name
                   << " Lafter="
                   << unmasks[lhs_ref2 + "##" + inst.rhs.name].name
                   << "\n";
      curRegion.instructions.push_back(
          Instruction{inst.op, inst.assignTo,
                      unmasks[rhs_ref2 + "##" + inst.lhs.name], inst.rhs});
      continue;
    }

    // Default:　do not change the instruction
    curRegion.instructions.push_back(inst);
  }
}

void FinalRegion::printAsCode(std::string_view func_name,
                              ValueInfo return_var,
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
    // llvm::errs() << "origin:" << vname << "\n";
  }
  for (const auto &var : curRegion.st) {
    const auto &vi = var.second;
    if (std::count(original_fparams.begin(), original_fparams.end(), vi.name)) {
      continue;
    }
    // Find all params declared in the function signature
    if ((vi.clangDecl && vi.clangDecl->getKind() == clang::Decl::ParmVar) ||
        vi.prop == VProp::RND) {
      // llvm::errs() << "added:" << vi.name << "\n";
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
  llvm::outs() << "return " << return_var.name << ";\n";
  llvm::outs() << "}\n";
}