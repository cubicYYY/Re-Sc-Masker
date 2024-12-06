#pragma once

#include "DataStructures.hpp"
#include <cassert>
#include <string_view>
#include <unordered_set>
#include <vector>

class TrivialMaskedRegion;
class DefUseCombinedRegion;

class MaskedRegion {};
class TrivialMaskedRegion : MaskedRegion {
public:
  TrivialMaskedRegion(Region &originalRegion);
  void dump() const;

private:
  void issueNewInst(std::vector<Instruction> &newInsts, std::string_view op,
                    const ValueInfo &t, const ValueInfo &a, const ValueInfo &b);
  std::vector<Instruction> replaceInstruction(const Instruction &inst);

public:
  Region region;
  std::unordered_set<ValueInfo> outputs;
};
