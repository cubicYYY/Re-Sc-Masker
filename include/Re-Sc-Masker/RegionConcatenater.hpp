#pragma once

#include <llvm-16/llvm/Support/raw_ostream.h>

#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Re-Sc-Masker/Config.hpp"
#include "Re-Sc-Masker/Preludes.hpp"

template <typename RegionCollectorT>
class RegionConcatenater : NonCopyable<RegionConcatenater<RegionCollectorT>> {
public:
    RegionConcatenater() = default;
    RegionConcatenater(RegionCollectorT &&r) {
        llvm::errs() << "---Composition---\n";
        std::map<std::string, ValueInfo> unmasks;

        // Pre-scan
        llvm::errs() << "--Pre-scanning--\n";
        r.dump();
        llvm::errs() << "#regions: " << r.regions.size() << "\n";

        // unordered_map: string -> unordered_set<string>
        typename RegionCollectorT::XorMap xor_diff;

        /// Temp symbol table: var to latest def statement.
        /// varname -> (#instruction in the output region)
        std::unordered_map<std::string, size_t> var2def;

        for (auto &&masked_region : r.regions) {
            for (auto &&inst : masked_region.insts) {
                inst.dump();
                if (inst.op == "=") {  // a move-assignment is found
                    auto real_lhs_i = find_n_update(r.alias_edge, inst.lhs.name);
                    if (real_lhs_i != r.alias_edge.end()) {
                        r.alias_edge[inst.res.name] = real_lhs_i->second;
                    }
                    llvm::errs() << "//=\n" << inst.toString() << "\n";
                    region.insts.emplace_back(std::move(inst));
                    continue;
                }

                if (inst.op != "^") {  // Ignore non-XOR instruction in swapping
                    region.insts.emplace_back(std::move(inst));
                    continue;
                }

                auto res = inst.res.name;  // do not find the root of ref chain, since a
                                           // new def will override this chain
                auto real_lhs_i = find_n_update(r.alias_edge, inst.lhs.name);
                auto real_rhs_i = find_n_update(r.alias_edge, inst.rhs.name);

                std::string real_lhs, real_rhs;
                if (real_lhs_i == r.alias_edge.end()) {
                    real_lhs = inst.lhs.name;
                } else {
                    real_lhs = real_lhs_i->second;
                }
                if (real_rhs_i == r.alias_edge.end()) {
                    real_rhs = inst.rhs.name;
                } else {
                    real_rhs = real_rhs_i->second;
                }

                bool is_def = r.output2xors.count(res);
                bool is_lhs_used = r.output2xors.count(real_lhs);
                bool is_rhs_used = r.output2xors.count(real_rhs);

                assert((int)is_def + (int)is_rhs_used + (int)is_lhs_used <= 1 && "Ambiguous use/def");
                // FIXME: We may have def+use like `t1=t2^r1`

                // Not def or use: change nothing
                if (!is_def && !is_lhs_used && !is_rhs_used) {
                    region.insts.emplace_back(std::move(inst));
                    continue;
                }

                // Def:
                if (is_def) {
                    llvm::errs() << "// def found: " << res << "\n";
                    var2def[res] = region.insts.size();

                    region.insts.emplace_back(std::move(inst));
                    continue;
                }

                // Use:
                if (is_lhs_used) {  // lhs is the output var: replace the rhs (random var)
                    llvm::errs() << "// lhs use found: " << real_lhs << "\n";
                    if (!xor_diff.count(real_lhs)) {  // first use: swapping
                        const Instruction &def_inst_ref = region.insts[var2def[real_lhs]];
                        xor_diff[real_lhs] = {real_rhs, def_inst_ref.rhs.name};
                        region.insts.emplace_back("^", inst.res, inst.lhs, def_inst_ref.rhs);
                        region.insts[var2def[real_lhs]].rhs = inst.rhs;  // replace the RAND var used in def
                        continue;
                    }
                    // If the swapping process have been perform on a var, we need to take the swapped RND var into
                    // account. For example, if we met the first use of X , like `n=X^r2`, then we need to replace the
                    // def `X=n^r1` with `n=X^r2`, and this use statement becomes `n=X^r1` (swapping). Then,
                    // xor_diff[X]={r1,r2}. However, after the first use, each time when we met another use "n=X^rx", we
                    // need to transform it into "n=X^rx^(xor_diff[X])" E.g. Original program: X=n^r1 n1=X^r2 n2=X^r3
                    //
                    // New program:
                    // X=n^r2 // swapped
                    // n1=X^r1 // first use, do swapping, xor_diff[X]={r1,r2}
                    // n2=X^r3; n2=n2^r1; n2=n2^r2; // new use
                    // (always add ^r3 before ^r1^r2, just like you need to add a new lock before removing old locks)

                    // Remember: only changes on def statement (of `X`) will have side-effects, so in this case the size
                    // of xor_diff[X]==2
                    const auto &diff = xor_diff[real_lhs];
                    assert(diff.size() == 2);
                    region.insts.emplace_back("//", "{replaced(" + real_lhs + "):");
                    region.insts.emplace_back(inst);
                    for (const auto &d : diff) {
                        region.insts.emplace_back("^", inst.res, inst.res,
                                                  ValueInfo{std::string_view(d), 1, VProp::RND, nullptr});
                    }
                    region.insts.emplace_back("//", ":replaced}");

                    continue;
                }

                if (is_rhs_used) {  // rhs is the output var: replace the lhs (random var)
                    llvm::errs() << "// rhs use found: " << real_rhs << "\n";
                    if (!xor_diff.count(real_rhs)) {  // first use: just swap the RND var
                        const Instruction &def_inst_ref = region.insts[var2def[real_rhs]];
                        xor_diff[real_rhs] = {real_lhs, def_inst_ref.rhs.name};
                        region.insts.emplace_back("^", inst.res, def_inst_ref.lhs, inst.rhs);
                        region.insts[var2def[real_rhs]].lhs = inst.lhs;
                        continue;
                    }

                    const auto &diff = xor_diff[real_rhs];
                    assert(diff.size() == 2);

                    region.insts.emplace_back("//", "{replaced(" + real_rhs + "):");
                    region.insts.emplace_back(inst);  // do not move it: still in use
                    for (const auto &d : diff) {
                        region.insts.emplace_back("^", inst.res, inst.res, ValueInfo{d, 1, VProp::RND, nullptr});
                    }
                    region.insts.emplace_back("//", ":replaced}");

                    continue;
                }

                // Default:　do not change the instruction
                region.insts.emplace_back(std::move(inst));
            }
            region.sym_tbl.insert(std::make_move_iterator(masked_region.sym_tbl.begin()),
                                  std::make_move_iterator(masked_region.sym_tbl.end()));
        }

        llvm::errs() << "GLOBAL SYM TBL:\n";
        for (const auto &[vname, vinfo] : r.global_sym_tbl) {
            llvm::errs() << vname << vinfo.toString() << "\n";
        }

        region.sym_tbl.insert(std::make_move_iterator(r.global_sym_tbl.begin()),
                              std::make_move_iterator(r.global_sym_tbl.end()));
    }

    void printAsCode(std::string_view func_name, ValueInfo return_var,
                     std::vector<std::string> original_fparams) const {
        const auto vname_regularizer = [](std::string_view var_name) {
            auto pos = var_name.find('#');
            return pos == std::string::npos ? std::string(var_name) : std::string(var_name).replace(pos, 1, "_");
        };

        std::vector<ValueInfo> temp_vars;
        llvm::errs() << "\n=====RESULT====="
                     << "\n";
        // func decl
        llvm::outs() << "bool " << func_name << "(";
        bool is_first_param = true;

        // Find all params declared in the function signature...
        for (const auto &vname : original_fparams) {
            if (is_first_param) {
                is_first_param = false;
            } else {
                llvm::outs() << ",";
            }
            llvm::outs() << "bool " << vname_regularizer(vname) << "=0";
        }

        // ...and those random variables introduced by us
        for (const auto &[vname, vinfo] : region.sym_tbl) {
            // Ignore those variables printed in func head
            if (std::count(original_fparams.begin(), original_fparams.end(), vinfo.name)) {
                continue;
            }
            // Find all params declared in the function signature
            if (vinfo.prop == VProp::RND) {
                if (is_first_param) {
                    is_first_param = false;
                } else {
                    llvm::outs() << ",";
                }
                llvm::outs() << "bool " << vname_regularizer(vname) << "=0";
            } else {
                temp_vars.emplace_back(vinfo);
            }
        }
        llvm::outs() << ")";

        // Function body
        llvm::outs() << "{\n";

        // local variable decl
        for (const auto &var : temp_vars) {
            llvm::outs() << "bool " << vname_regularizer(var.name) << ";\n";
        }

        // insts
        for (const auto &instruction : region.insts) {
            llvm::outs() << instruction.toRegularizedString(vname_regularizer) << "\n";
        }
        llvm::outs() << "return " << vname_regularizer(return_var.name) << ";\n";
        llvm::outs() << "}\n";
    }

public:
    Region region;
};
