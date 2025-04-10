#include "Re-Sc-Masker/RegionDivider.hpp"

#include "Re-Sc-Masker/Preludes.hpp"

/// Return the next region
Region TrivialRegionDivider::next() {
    auto result = Region();
    if (cur == global_region.count()) {
        return result;  // end.
    }
    auto const &curInst = global_region.instructions[cur];
    result.instructions.push_back(curInst);
    result.instructions.push_back(Instruction("//", "------region-----"));
    cur++;
    return result;
}

bool TrivialRegionDivider::done() const { return cur >= global_region.count(); }
