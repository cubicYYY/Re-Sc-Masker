#include "Re-Sc-Masker/RegionDivider.hpp"

#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"

// TODO: should be trivial divider + no divider

TrivialRegionDivider::TrivialRegionDivider(Region &&global_region) : global_sym_tbl(std::move(global_region.sym_tbl)) {
    for (auto &&inst : global_region.insts) {
        // each instruction is a region
        regions.emplace_back(std::vector<Instruction>{std::move(inst)});
    }
}
