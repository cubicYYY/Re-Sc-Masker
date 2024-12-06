#pragma once

#include "DataStructures.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>
#include <string>
#include <string_view>

class DefUseCombinedRegion;
class FinalRegion {
public:
  FinalRegion() = default;
  FinalRegion(DefUseCombinedRegion &&r);
  void printAsCode(std::string_view func_name, std::string_view return_name,
                   std::vector<std::string> original_fparams) const;

public:
  Region curRegion;
};
