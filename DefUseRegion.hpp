#pragma once

#include "DataStructures.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

class TrivialMaskedRegion;

/// Combined masked regions with "leaky gap" between each region fixed
class DefUseCombinedRegion {
public:
  DefUseCombinedRegion() = default;
  void add(TrivialMaskedRegion &&r);
  void dump();

public:
  Region region;

  /// XOR-ed variables in def or use, updated by `add`
  using XorSet = std::unordered_set<ValueInfo>;
  using XorMap = std::unordered_map<std::string, XorSet>;
  XorMap outputs2xors;

  // e.g. k!5 -> n1
  // NOTE: a reference chain (a->b->c->...->t) will be flattened to a->t
  // NOTE: if t is the root, t->t will be added to the map
  std::unordered_map<std::string, std::string> alias2var;

};
