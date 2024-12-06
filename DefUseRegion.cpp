#include "DefUseRegion.hpp"
#include "MaskedRegion.hpp"
#include <llvm-16/llvm/Support/raw_ostream.h>

void DefUseCombinedRegion::dump() {
  llvm::errs() << "dumping: dur\n";
  region.dump();
  for (auto out : outputs2xored) {
    llvm::errs() << out.first << "->";
    for (auto xored : out.second) {
      llvm::errs() << xored.name << ";";
    }
    llvm::errs() << "\n";
  }
}

void DefUseCombinedRegion::add(TrivialMaskedRegion &&r) {
  llvm::errs() << "adding:\n";
  r.dump();

  // Scan and collect all RND/UND vars one get XOR-ed with
  for (const auto &inst : r.region.instructions) {
    llvm::errs() << "checking:" << inst.assignTo.name << "\n";
    // Def
    if (r.outputs.find(inst.assignTo) != r.outputs.end()) {
      llvm::errs() << "def:" << inst.assignTo.name;
      assert(inst.op == "^" && "Not supported!");
      // FIXME:
      // assert(inst.lhs.prop == VProp::RND || inst.rhs.prop == VProp::RND);

      // We prefer the right side
      const auto &rand_oprand =
          (inst.lhs.prop == VProp::RND
               ? (inst.rhs.prop == VProp::RND ? inst.rhs : inst.lhs)
               : inst.rhs);
      outputs2xored[inst.assignTo.name].insert(rand_oprand);
      continue;
    }
    // Use
    if (outputs2xored.find(inst.rhs.name) != outputs2xored.end()) {
      assert(inst.op == "^" && "Not supported!");
      assert(inst.lhs.prop == VProp::RND);
      outputs2xored[inst.rhs.name].insert(inst.lhs);
      continue;
    }
    if (outputs2xored.find(inst.lhs.name) != outputs2xored.end()) {
      assert(inst.op == "^" && "Not supported!");
      assert(inst.rhs.prop == VProp::RND);
      outputs2xored[inst.lhs.name].insert(inst.rhs);
      continue;
    }
  }

  // TODO: move
  region.instructions.insert(region.instructions.end(),
                             r.region.instructions.begin(),
                             r.region.instructions.end());
  region.st.insert(r.region.st.begin(), r.region.st.end());
}
