#pragma once

#include <llvm-16/llvm/Support/raw_ostream.h>

#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Re-Sc-Masker/Preludes.hpp"
#include "Re-Sc-Masker/Config.hpp"

template <typename DefUseRegionType>
class RegionConcatenater {
public:
    RegionConcatenater() = default;
    RegionConcatenater(DefUseRegionType &&r) {
        llvm::errs() << "---Composition---\n";
        std::map<std::string, ValueInfo> unmasks;

        // Pre-scan
        llvm::errs() << "--Pre-scanning--\n";
        r.dump();
        llvm::errs() << "outputs2xors:\n";
        for (const auto &xor_set : r.output2xors) {
            llvm::errs() << xor_set.first << ": ";
            for (const auto &xored : xor_set.second) {
                llvm::errs() << xored << " ";
            }
            llvm::errs() << "\n";
        }
        llvm::errs() << "----------\n";

        typename DefUseRegionType::XorMap xor_diff;

        /// Temp symbol table: var to latest def statement.
        /// varname -> (#instruction in the output region)
        std::unordered_map<std::string, size_t> var2def;

        for (const auto &masked_region : r.regions) {
            for (const auto &inst : masked_region.region.instructions) {
                inst.dump();
                if (inst.op != "^") {
                    curRegion.instructions.push_back(inst);
                    continue;
                }
                if (inst.op == "=") {
                    curRegion.instructions.push_back(inst);
                    auto lhs_ref2 = find_root(r.alias_edge, inst.lhs.name);
                    r.alias_edge[inst.assign_to.name] = lhs_ref2;
                    continue;
                }

#ifdef SCM_GAP_FILLING_ENABLED
                // IMPORTANT NOTE: We may have def+use like `t1=t2^r1`

                auto assign_to = inst.assign_to.name;  // do not find the root of ref chain, since a
                                                       // new def will override this chain
                auto rhs_ref2 = find_root(r.alias_edge, inst.rhs.name);
                auto lhs_ref2 = find_root(r.alias_edge, inst.lhs.name);
                bool is_def = r.output2xors.count(assign_to);
                bool is_r_use = r.output2xors.count(rhs_ref2) &&
                                r.output2xors[rhs_ref2].count(lhs_ref2);  // lhs is a var used in rhs's XOR
                                                                          // set, so it is a use of rhs
                bool is_l_use = r.output2xors.count(lhs_ref2) &&
                                r.output2xors[lhs_ref2].count(rhs_ref2);  // rhs is a var used in lhs's XOR
                                                                          // set, so it is  ause of lhs

                if (!is_def && !is_l_use && !is_r_use) {
                    curRegion.instructions.push_back(inst);
                    continue;
                }

                // 1. Def+Use
                // TODO: unimplemented
                if (is_def && is_l_use && is_r_use) {
                    assert(false && "ubnimplemented");
                    continue;
                }

                // 2. Def
                if (is_def) {
                    curRegion.instructions.push_back(Instruction{"//", "def:"});

                    var2def[assign_to] = curRegion.instructions.size();

                    curRegion.instructions.push_back(inst);
                    continue;
                }

                // 3. Use
                if (is_l_use) {                       // lhs is the output var: replace
                                                      // the rhs (random var)
                    if (!xor_diff.count(lhs_ref2)) {  // first use: swapping
                        Instruction &def_inst_ref = curRegion.instructions[var2def[lhs_ref2]];
                        xor_diff[lhs_ref2] = {rhs_ref2, def_inst_ref.rhs.name};
                        curRegion.instructions.push_back(Instruction{"^", inst.assign_to, inst.lhs, def_inst_ref.rhs});
                        def_inst_ref.rhs = inst.rhs;
                        continue;
                    }
                    // otherwise, we need to take the swapped RND var into account.
                    // For example, if we met the first use of X , like `n=X^r2`, then we need
                    // to replace the def `X=n^r1` with `n=X^r2`, and this use statement becomes `n=X^r1`
                    // (swapping). Then, xor_diff[X]={r1,r2}.
                    // However, after the first use, each time when we
                    // met another use "n=X^rx", we need to transform it into "n=X^rx^(xor_diff[X])"
                    // E.g.
                    // Original program:
                    // X=n^r1
                    // n1=X^r2
                    // n2=X^r3
                    //
                    // New program:
                    // X=n^r2 // swapped
                    // n1=X^r1 // first use, do swapping, xor_diff[X]={r1,r2}
                    // n2=X^r3; n2=n2^r1; n2=n2^r2; // new use
                    // (always add ^r3 before ^r1^r2, just like you need to add a new lock before removing old locks)

                    // Remember: only changes on def statement (of `X`) will have side-effects, so in this case the size
                    // of xor_diff[X]==2
                    const auto &diff = xor_diff[lhs_ref2];
                    assert(diff.size() == 2);
                    curRegion.instructions.push_back(Instruction{"//", "{replaced(" + lhs_ref2 + "):"});
                    curRegion.instructions.push_back(inst);
                    for (const auto &d : diff) {
                        curRegion.instructions.push_back(
                            Instruction{"^", inst.assign_to, inst.assign_to, ValueInfo{d, 1, VProp::RND, nullptr}});
                    }
                    curRegion.instructions.push_back(Instruction{"//", ":replaced}"});

                    continue;
                }

                if (is_r_use) {                       // rhs is the output var: replace
                                                      // the lhs (random var)
                    if (!xor_diff.count(rhs_ref2)) {  // first use: just swap the RND var
                        Instruction &def_inst_ref = curRegion.instructions[var2def[rhs_ref2]];
                        xor_diff[rhs_ref2] = {lhs_ref2, def_inst_ref.rhs.name};
                        curRegion.instructions.push_back(Instruction{"^", inst.assign_to, def_inst_ref.lhs, inst.rhs});
                        def_inst_ref.lhs = inst.lhs;
                        continue;
                    }

                    const auto &diff = xor_diff[rhs_ref2];
                    assert(diff.size() == 2);

                    curRegion.instructions.push_back(Instruction{"//", "{replaced(" + rhs_ref2 + "):"});
                    curRegion.instructions.push_back(inst);
                    for (const auto &d : diff) {
                        curRegion.instructions.push_back(
                            Instruction{"^", inst.assign_to, inst.assign_to, ValueInfo{d, 1, VProp::RND, nullptr}});
                    }
                    curRegion.instructions.push_back(Instruction{"//", ":replaced}"});

                    continue;
                }
#endif

                // Default:　do not change the instruction
                curRegion.instructions.push_back(inst);
            }
            curRegion.st.insert(masked_region.region.st.begin(), masked_region.region.st.end());
        }
    }
    void printAsCode(std::string_view func_name, ValueInfo return_var,
                     std::vector<std::string> original_fparams) const {
        std::vector<ValueInfo> tempVars;
        llvm::errs() << "\n=====RESULT====="
                     << "\n";
        // func decl
        llvm::outs() << "bool " << func_name << "(";
        bool isFirstParam = true;

        // Find all params declared in the function signature...
        for (const auto &vname : original_fparams) {
            if (isFirstParam) {
                isFirstParam = false;
            } else {
                llvm::outs() << ",";
            }
            llvm::outs() << "bool " << vname << "=0";  // TODO: remove the default value
                                                       // llvm::errs() << "origin:" << vname << "\n";
        }

        // ...and those random variables introduced by us
        for (const auto &var : curRegion.st) {
            const auto &vi = var.second;
            // Ignore those variables printed in func head
            if (std::count(original_fparams.begin(), original_fparams.end(), vi.name)) {
                continue;
            }
            // Find all params declared in the function signature
            if (vi.prop == VProp::RND) {
                // llvm::errs() << "added:" << vi.name << "\n";
                if (isFirstParam) {
                    isFirstParam = false;
                } else {
                    llvm::outs() << ",";
                }
                llvm::outs() << "bool " << vi.name << "=0";  // TODO: remove the default value
            } else {
                tempVars.push_back(vi);
            }
        }
        llvm::outs() << ")";

        // Function body
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

public:
    Region curRegion;
};
