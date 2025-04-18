#include "Re-Sc-Masker/RegionDivider.hpp"

#include "Re-Sc-Masker/Preludes.hpp"

/// Return the next region
Region TrivialRegionDivider::next() {
    auto result = Region();
    if (cur == global_region.count()) {
        return result;  // end.
    }
    result.insts.emplace_back(global_region.insts[cur]);
    result.insts.emplace_back("//", "------region-----");
    cur++;
    return result;
}

bool TrivialRegionDivider::done() const { return cur >= global_region.count(); }
