#include "DefUseRegion.hpp"
#include "MaskedRegion.hpp"
#include <llvm-16/llvm/Support/raw_ostream.h>

void DefUseCombinedRegion::dump() {
  llvm::errs() << "dumping: dur\n";
  region.dump();
  for (auto out : outputs2xors) {
    llvm::errs() << out.first << "->";
    for (auto xored : out.second) {
      llvm::errs() << xored.name << ";";
    }
    llvm::errs() << "\n";
  }
  // Print alias2var mappings
  llvm::errs() << "alias mappings:\n";
  for (const auto &alias : alias2var) {
    llvm::errs() << alias.first << " -> " << alias.second << "\n";
  }

}

void DefUseCombinedRegion::add(TrivialMaskedRegion &&r) {
  llvm::errs() << "ADDING:\n";
  r.dump();
  // Print outputs
  llvm::errs() << "outputs: ";
  for (const auto &out : r.outputs) {
    llvm::errs() << out.name << " ";
  }
  llvm::errs() << "\n";
  // print outputs2xors
  llvm::errs() << "outputs2xors: ";
  for (const auto &out : outputs2xors) {
    llvm::errs() << out.first << ": ";
    for (const auto &xorvar : out.second) {
      llvm::errs() << xorvar.name << " ";
    }
    llvm::errs() << "\n";
  }
  llvm::errs() << "______\n";
  // FIXME: this should be changed if we are not masking trivially
  // We should only find those RND/UND vars at the first use or the final use in
  // each region

  // Check all outputs
  llvm::errs() << "=====Checking outputs:\n";
  for (const auto &out : r.outputs) {
    llvm::errs() << out.name << "\n";
    
  }
  llvm::errs() << "=====\n";

  // Scan and collect XorS
  XorMap added_vars;
  for (const auto &inst : r.region.instructions) {
    // Alias
    if (inst.op == "=" && r.outputs.count(inst.assignTo)) {
      llvm::errs() << "Alias:" << inst.assignTo.name << " = " << inst.lhs.name << "\n";
      // Find root by following the reference chain
      std::string root = inst.lhs.name;
      while (alias2var.count(root)) {
        root = alias2var[root];
      }
      alias2var[inst.assignTo.name] = root;
    }

    // Def
    if (r.outputs.count(inst.assignTo)) { // This def needs to be exposed to the next region
        // FIXME: Really? Should we actually check all previously undefined vars?
      llvm::errs() << "DEF:" << inst.assignTo.name << "\n";
      inst.dump();
      if (inst.op != "^") {
        continue;
      }
      // FIXME:
      // assert(inst.lhs.prop == VProp::RND || inst.rhs.prop == VProp::RND);

      // We prefer the right side
      const auto &rand_oprand =
          (inst.lhs.prop == VProp::RND
               ? (inst.rhs.prop == VProp::RND ? inst.rhs : inst.lhs)
               : inst.rhs);
      added_vars[inst.assignTo.name].insert(rand_oprand);
      continue;
    }

    // Use
    // FIXME: Only for the first use of each variable in each region!
    auto first_use = true;

    auto actual_use_r = alias2var.count(inst.rhs.name);
    auto actual_use_l = alias2var.count(inst.lhs.name);
    if (actual_use_r) {
      llvm::errs() << "USE:" << inst.rhs.name << "\n";
      if (inst.op != "^") {
        continue;
      }
      assert(inst.lhs.prop == VProp::RND);
      added_vars[alias2var[inst.rhs.name]].insert(inst.lhs);
      continue;
    }
    if (actual_use_l) {
      llvm::errs() << "USE:" << inst.lhs.name << "\n";
      if (inst.op != "^") {
        continue;
      }
      assert(inst.rhs.prop == VProp::RND);
      added_vars[alias2var[inst.lhs.name]].insert(inst.rhs);
      continue;
    }
  }

  // Extend the map
  for (const auto &[var, xorset] : added_vars) {
    outputs2xors[var].insert(xorset.begin(), xorset.end());
  }

  // TODO: move
  region.instructions.insert(region.instructions.end(),
                             r.region.instructions.begin(),
                             r.region.instructions.end());
  region.st.insert(r.region.st.begin(), r.region.st.end());
}
