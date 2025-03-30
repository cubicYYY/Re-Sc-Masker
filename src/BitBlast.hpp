#pragma once

#include "DataStructures.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <z3++.h>

class Z3VInfo;

/// Utilizes Z3 Solver to eliminate integer operations
class BitBlastPass : public Pass {
public:
  /// Encode relationship between separated bits in Z3
  BitBlastPass(SymbolTable st, ValueInfo ret);

  /// Encode relationship between separated bits in Z3
  void add(Region &&r);

  /// Return the bit-blasted pass
  Region get() override;

private:
  /// Traverse the model to get the representation of output variables
  Z3VInfo traverseZ3Model(const z3::expr &e, int indent = 0);

private:
  z3::context z3ctx;
  z3::goal z3goal{z3ctx};

  std::unordered_map<ValueInfo, std::optional<z3::expr>> var_expr{};

  /// var -> z3 expr for each bit in the var
  std::unordered_map<ValueInfo, std::vector<std::optional<z3::expr>>>
      var_bits{};
  /// e.g. `k!4` -> `|out#3|`
  std::unordered_map<int, std::string> alias_of;
  /// e.g. `|out#3|` -> `k!4`
  std::unordered_map<std::string, int> z3alias;

  Region region;
  ValueInfo ret;
};

enum class Z3VType { Other, Input, Output };

class Z3VInfo {
public:
  Z3VInfo(): name(""), type(Z3VType::Other) {}
  Z3VInfo(const Z3VInfo& o): name(o.name), type(o.type) {}
  Z3VInfo(std::string_view name, Z3VType type) : name(name), type(type) {}
  static std::string getNewName() {
    static int id = 0;
    return "z3_" + std::to_string(id++);
  }

public:
  std::string name;
  Z3VType type;
};