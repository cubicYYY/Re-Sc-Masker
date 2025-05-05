#pragma once

#include <llvm-16/llvm/Support/raw_ostream.h>

#include <cassert>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"

/// calculate XorSet; collect all alias relationships
template <typename RegionDividerType>
class RegionCollector {
public:
    explicit RegionCollector(RegionDividerType &&masked_region)
        : global_sym_tbl(std::move(masked_region.global_sym_tbl)) {
        for (auto &&rio : masked_region.regions_io) {
            // Check all outputs
            // FIXME: we shall not throw all vars into outputs set... only those who will be used afterwards
            llvm::errs() << "\n=====Checking outputs:\n";
            for (const auto &out : rio.outs) {
                llvm::errs() << out.name << "\n";
            }
            llvm::errs() << "=====\n\n";

            // Scan and collect XorS for each var
            XorMap xored_vars;

            for (const auto &inst : rio.r.insts) {
                // Alias
                if (inst.op == "=") {
                    llvm::errs() << "Alias:" << inst.res.name << " = " << inst.lhs.name << "\n";
                    // Find root by following the reference chain,
                    // then update the alias edges
                    auto root = find_n_update(alias_edge, inst.res.name);
                    if (root != alias_edge.end()) {
                        alias_edge[inst.res.name] = root->second;
                    }
                }

                // Xor Def
                if (rio.outs.count(inst.res) && inst.op == "^") {
                    // This def needs to be exposed to the next region
                    // FIXME: currently we assume that every output var will be used.
                    llvm::errs() << "DEF:" << inst.res.name << "\n";
                    inst.dump();
                    // FIXME: assert(inst.lhs.prop == VProp::RND || inst.rhs.prop == VProp::RND);
                    // FIXME: We should ensure that only the first def of a var in a region should
                    // be considered. Currently we dont check this since no re-declaration
                    // happens with the trivial masking strategy.

                    // We prefer the right side if both side is RAND.
                    const auto &rand_oprand =
                        (inst.lhs.prop == VProp::RND ? (inst.rhs.prop == VProp::RND ? inst.rhs : inst.lhs) : inst.rhs);
                    xored_vars[inst.res.name].insert(rand_oprand.name);
                    continue;
                }

                // Xor Use
                // FIXME: We should ensure that only the first use of a var in a region should
                // be considered. Currently we dont check this since no re-declaration
                // happens for trivial masking strategy. So this flag var is not used.
                // So this should be evaluated in MaskedRegion !!!
                auto _first_use = true;

                // Find var being actual used (i.e., the root of alias chain)
                auto is_rhs_actual_used = bool(alias_edge.count(inst.rhs.name));
                auto is_lhs_actual_used = bool(alias_edge.count(inst.lhs.name));
                if (is_rhs_actual_used && inst.op == "^") {
                    llvm::errs() << "USE:" << inst.rhs.name << "\n";

                    auto root = find_n_update(alias_edge, inst.rhs.name);
                    std::string real_name;
                    if (root != alias_edge.end()) {
                        real_name = inst.rhs.name;
                    } else {
                        real_name = root->second;
                    }

                    assert(inst.lhs.prop == VProp::RND);
                    xored_vars[real_name].insert(inst.lhs.name);
                    continue;
                }
                if (is_lhs_actual_used && inst.op == "^") {
                    llvm::errs() << "USE:" << inst.lhs.name << "\n";

                    auto root = find_n_update(alias_edge, inst.lhs.name);
                    std::string real_name;
                    if (root != alias_edge.end()) {
                        real_name = inst.lhs.name;
                    } else {
                        real_name = root->second;
                    }

                    assert(inst.rhs.prop == VProp::RND);
                    xored_vars[real_name].insert(inst.rhs.name);
                    continue;
                }
            }

            // Extend the map
            for (const auto &[var, xorset] : xored_vars) {
                output2xors[var].insert(xorset.begin(), xorset.end());
            }

            regions.emplace_back(std::move(rio.r));
        }
    }
    RegionCollector(RegionCollector &&) = delete;
    RegionCollector(const RegionCollector &) = delete;

    void dump() const {
        llvm::errs() << "\n- Regions:\n";
        for (const auto &r : regions) {
            r.dump();
        }
        llvm::errs() << "\n- Xor Mappings:\n";
        for (const auto &out : output2xors) {
            llvm::errs() << out.first << "->";
            for (const auto &xored : out.second) {
                llvm::errs() << xored << ";";
            }
            llvm::errs() << "\n";
        }
        // Print alias_edge mappings
        llvm::errs() << "\n- alias mappings:\n";
        for (const auto &alias : alias_edge) {
            llvm::errs() << alias.first << " -> " << alias.second << "\n";
        }
    }

public:
    /// XOR-ed variables in def or use, updated by `add`
    using StrSet = std::unordered_set<std::string>;
    using XorMap = std::unordered_map<std::string, StrSet>;

    // all regions
    std::vector<Region> regions;

    // Global information of the XorSet of each var
    // !FIXME: ValueInfo->ValueSet instead of name->ValueSet
    XorMap output2xors;

    // e.g. k!5 -> n1
    // NOTE: a reference chain (a->b->c->...->t->end()) will be flattened to a->t
    // Which means every declared var is recorded in keys
    std::unordered_map<std::string, std::string> alias_edge;

    SymbolTable global_sym_tbl;
};
