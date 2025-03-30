#include "RegionDivider.hpp"

#include "DataStructures.hpp"

/// Return the next region
Region TrivialRegionDivider::next() {
  auto result = Region();
  if (cur == r.count()) {
    return result;  // end.
  }
  auto const &curInst = r.instructions[cur];
  result.instructions.push_back(curInst);
  result.instructions.push_back(Instruction("//", "------region-----"));
  cur++;
  return result;
}

bool TrivialRegionDivider::done() const { return cur >= r.count(); }
