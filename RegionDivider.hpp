#pragma once

#include "DataStructures.hpp"

class RegionDivider {};

/// assign a region for each instruction
class TrivialRegionDivider : RegionDivider {
public:
  TrivialRegionDivider(const Region &r) : r(r), cur(0) {}
  /// Return the next region
  Region next() {
    auto result = Region();
    if (cur == r.count()) {
      return result; // end.
    }
    auto const &curInst = r.instructions[cur];
    result.append(curInst);
    r.outputs2xored[curInst.assignTo.name] = {};
    result.outputs2xored.insert(r.outputs2xored.begin(),
                                r.outputs2xored.end()); // clone
    cur++;
    return result;
  }

  bool done() const { return cur >= r.count(); }

private:
  Region r;
  size_t cur;
};
