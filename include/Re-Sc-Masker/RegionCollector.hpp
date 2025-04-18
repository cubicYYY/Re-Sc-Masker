#pragma once

#include <llvm-16/llvm/Support/raw_ostream.h>

#include <cassert>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"

class TrivialRegionMasker;

template <typename RegionMaskerType>
class RegionCollector {
public:
    explicit RegionCollector() = default;
    RegionCollector(RegionCollector &&) = delete;
    RegionCollector(const RegionCollector &) = delete;

    void add(RegionMaskerType &&r) {
        llvm::errs() << "ADDING:\n";
        // r.dump();
        // Print outputs
        llvm::errs() << "region outputs: ";
        for (const auto &out : r.outputs) {
            llvm::errs() << out.name << " ";
        }
        llvm::errs() << "\n";
        llvm::errs() << "______\n";

        // Check all outputs
        // FIXME: we shall not throw all vars into outputs set
        // We should only find those RND/UND vars at the first use or the final use
        // in each region
        llvm::errs() << "\n=====Checking outputs:\n";
        for (const auto &out : r.outputs) {
            llvm::errs() << out.name << "\n";
        }
        llvm::errs() << "=====\n\n";

        // Scan and collect XorS
        XorMap regional_xored_vars;

        for (const auto &inst : r.region.insts) {
            // Alias
            if (inst.op == "=") {
                llvm::errs() << "Alias:" << inst.res.name << " = " << inst.lhs.name << "\n";
                // Find root by following the reference chain
                std::string root = inst.lhs.name;
                while (alias_edge.count(root) && root != alias_edge[root]) {
                    root = alias_edge[root];
                }
                alias_edge[inst.res.name] = root;
            }

            // Xor Def
            if (r.outputs.count(inst.res)) {
                // This def needs to be exposed to the next region
                // FIXME: currently we assume that every output var will be used.
                llvm::errs() << "DEF:" << inst.res.name << "\n";
                inst.dump();
                if (inst.op != "^") {
                    continue;
                }
                // FIXME: assert(inst.lhs.prop == VProp::RND || inst.rhs.prop == VProp::RND);
                // FIXME: We should ensure that only the first def of a var in a region should
                // be considered. Currently we dont check this since no re-declaration
                // happens for trivial masking strategy.

                // We prefer the right side if both side is RAND.
                const auto &rand_oprand =
                    (inst.lhs.prop == VProp::RND ? (inst.rhs.prop == VProp::RND ? inst.rhs : inst.lhs) : inst.rhs);
                regional_xored_vars[inst.res.name].insert(rand_oprand.name);
                alias_edge[inst.res.name] = inst.res.name;  // t->t should also be added
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
            if (is_rhs_actual_used) {
                llvm::errs() << "USE:" << inst.rhs.name << "\n";
                if (inst.op != "^") {
                    continue;
                }

                std::string root = inst.rhs.name;
                while (alias_edge.count(root) && root != alias_edge[root]) {
                    root = alias_edge[root];
                }

                assert(inst.lhs.prop == VProp::RND);
                regional_xored_vars[root].insert(inst.lhs.name);
                continue;
            }
            if (is_lhs_actual_used) {
                llvm::errs() << "USE:" << inst.lhs.name << "\n";
                if (inst.op != "^") {
                    continue;
                }
                std::string root = inst.lhs.name;
                while (alias_edge.count(root) && root != alias_edge[root]) {
                    root = alias_edge[root];
                }

                assert(inst.rhs.prop == VProp::RND);
                regional_xored_vars[root].insert(inst.rhs.name);
                continue;
            }
        }

        // Extend the map
        for (const auto &[var, xorset] : regional_xored_vars) {
            output2xors[var].insert(xorset.begin(), xorset.end());
        }

        regions.emplace_back(std::move(r));
    }

    void dump() const {
        llvm::errs() << "\n- Regions:\n";
        for (const auto &r : regions) {
            r.dump();
        }
        llvm::errs() << "\n- Xor Mappings:\n";
        for (auto out : output2xors) {
            llvm::errs() << out.first << "->";
            for (auto xored : out.second) {
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
    std::vector<RegionMaskerType> regions;

    // each region's defs
    // std::vector<ValueSet> defs;

    // each region's uses
    // std::vector<ValueSet> uses;

    // Global information of the XorSet of each var
    // !FIXME: ValueInfo->ValueSet instead of name->ValueSet
    XorMap output2xors;

    // e.g. k!5 -> n1
    // NOTE: a reference chain (a->b->c->...->t) will be flattened to a->t
    // NOTE: if t is the root, t->t will be added to the map
    // Which means every declared var is recorded in keys
    // FIXME: we should ensure that every chain ends with a self-pointed circle
    // (similar to find-union-set)
    std::unordered_map<std::string, std::string> alias_edge;
};
