#pragma once

#include "DataStructures.hpp"

class RegionDivider {};

/// simply assign a region for each instruction
class TrivialRegionDivider : RegionDivider {
public:
    TrivialRegionDivider(const Region &global_region) : global_region(global_region), cur(0) {}
    /// Return the next region
    Region next();

    bool done() const;

private:
    Region global_region;
    size_t cur;
};
