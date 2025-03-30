#pragma once

#include <cassert>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "DataStructures.hpp"

class TrivialMaskedRegion;
template <typename MaskedRegionType>
class DefUseRegion;

class MaskedRegion {};
class TrivialMaskedRegion : MaskedRegion {
public:
    TrivialMaskedRegion(Region &originalRegion);
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
