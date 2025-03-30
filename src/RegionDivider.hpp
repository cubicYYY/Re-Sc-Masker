#pragma once

#include "DataStructures.hpp"

class RegionDivider {};

/// assign a region for each instruction
class TrivialRegionDivider : RegionDivider {
public:
    TrivialRegionDivider(const Region &r) : r(r), cur(0) {}
    /// Return the next region
    Region next();

    bool done() const;

private:
    Region r;
    size_t cur;
};
