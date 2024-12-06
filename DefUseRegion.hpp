#pragma once

#include "DataStructures.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>
#include <unordered_set>

class TrivialMaskedRegion;
class DefUseCombinedRegion {
public:
  DefUseCombinedRegion() = default;
  void add(TrivialMaskedRegion &&r);
  void dump();

public:
  Region region;

  /// XOR-ed variables in def or use
  std::unordered_map<std::string, std::unordered_set<ValueInfo>> outputs2xored;
};
