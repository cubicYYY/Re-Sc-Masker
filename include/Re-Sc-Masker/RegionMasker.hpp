#pragma once

#include <cassert>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"

template <typename RegionMaskerType>
class RegionCollector;  // FIXME: remove this after the special hack to handle operator "|" is resolved

class RegionMasker : private NonCopyable<RegionMasker> {};

class TrivialRegionMasker : RegionMasker {
public:
    TrivialRegionMasker(Region &originalRegion);
    TrivialRegionMasker(TrivialRegionMasker &&other) noexcept;
    TrivialRegionMasker &operator=(TrivialRegionMasker &&other) noexcept;
    void dump() const;

private:
    void issueNewInst(std::vector<Instruction> &newInsts, std::string_view op, const ValueInfo &t, const ValueInfo &a,
                      const ValueInfo &b);
    std::vector<Instruction> replaceInstruction(const Instruction &inst);

public:
    Region region;

    // potential outputs (may not be used in regions followed)
    std::unordered_set<ValueInfo> outputs;
};
