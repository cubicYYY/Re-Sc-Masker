#pragma once

#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"

class RegionDivider : NonCopyable<RegionDivider> {};

/// simply assign a region for each instruction
class TrivialRegionDivider : RegionDivider {
public:
    TrivialRegionDivider(Region &&global_region);
    std::vector<Region> regions;
    SymbolTable global_sym_tbl;
};
