#pragma once

#include "clang/AST/Decl.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Helper function to convert a string to a valid variable name
inline std::string toValidVarName(std::string_view str) {
  std::string result;
  result.reserve(str.length());

  // First character must be letter or underscore
  if (!str.empty()) {
    if (std::isalpha(str[0]) || str[0] == '_') {
      result += str[0];
    } else {
      if (str[0] == '!') {
        result += "_not_";
      } else if (str[0] == '#') {
        result += "_hash_";
      } else {
        result += '_';
      }
    }
  }

  // Subsequent characters can be alphanumeric or underscore
  for (size_t i = 1; i < str.length(); i++) {
    if (std::isalnum(str[i]) || str[i] == '_') {
      result += str[i];
    } else {
      if (str[i] == '!') {
        result += "_not_";
      } else if (str[i] == '#') {
        result += "_hash_";
      } else {
        result += '_';
      }
    }
  }

  return result;
}

// Variable property
enum class VProp {
  UNK,
  MASKED,
  PUB,
  RND,
  CST,
  SECRET,
  OUTPUT,
};
inline std::string toString(VProp v) {
  switch (v) {
  case VProp::UNK:
    return "UNK";
  case VProp::MASKED:
    return "MASKED";
  case VProp::PUB:
    return "PUB";
  case VProp::RND:
    return "RND";
  case VProp::CST:
    return "CST";
  case VProp::SECRET:
    return "SECRET";
  case VProp::OUTPUT:
    return "OUTPUT";
  default:
    return "Unknown";
  }
}

// +: uint, -:int
using Width = int;

/// TODO: auto insertion into an optional SymbolTable
class ValueInfo {
public:
  ValueInfo() : name(""), width(0), prop(VProp::UNK), clangDecl(nullptr) {}
  ValueInfo(std::string_view name, Width width, VProp prop,
            const clang::VarDecl *clangDecl)
      : name(name), width(width), prop(prop), clangDecl(clangDecl) {}
  ValueInfo(ValueInfo &&val) noexcept
      : name(val.name), width(val.width), prop(val.prop),
        clangDecl(val.clangDecl) {}
  ValueInfo(const ValueInfo &val)
      : name(val.name), width(val.width), prop(val.prop),
        clangDecl(val.clangDecl) {}

  // Copy Assignment Operator
  ValueInfo &operator=(const ValueInfo &other) {
    name = other.name;
    width = other.width;
    prop = other.prop;
    clangDecl = other.clangDecl;
    return *this;
  }

  // Move Assignment Operator
  ValueInfo &operator=(ValueInfo &&other) noexcept {
    name = other.name;
    width = other.width;
    prop = other.prop;
    clangDecl = other.clangDecl;
    return *this;
  }

  bool operator==(const ValueInfo &other) const {
    return name == other.name && clangDecl == other.clangDecl &&
           width == other.width && prop == other.prop;
  }

  bool operator!=(const ValueInfo &other) const { return !(*this == other); }

  /// TODO: accept a SymbolTable ref to update
  static ValueInfo getNewRand() {
    const size_t ID_START = 100;
    static std::atomic<size_t> currentId{ID_START};

    std::string newName = "r" + std::to_string(currentId++);
    return ValueInfo(newName, 1, VProp::RND, nullptr);
  }

  bool isNone() const {
    return width == 0 && prop == VProp::UNK && clangDecl == nullptr;
  }

public:
  std::string name;
  Width width;
  VProp prop;
  /// NOTE: may be nullptr!
  const clang::VarDecl *clangDecl;
};

// Define a hash function for ValueInfo, so that it can be used in
// std::unordered_set
namespace std {
template <> struct hash<ValueInfo> {
  std::size_t operator()(const ValueInfo &v) const {
    if (!v.clangDecl) {
      return v.name.length();
    }
    return v.clangDecl->getGlobalID();
  }
};
} // namespace std

using SymbolTable = std::unordered_map<std::string, ValueInfo>;

class Instruction {
public:
  Instruction() {}

  Instruction(std::string_view op, std::string_view content)
      : op(op), assign_to(ValueInfo{content, 0, VProp::PUB, nullptr}),
        lhs(ValueInfo{}), rhs(ValueInfo{}) {}

  Instruction(std::string_view op, ValueInfo assign_to, ValueInfo lhs,
              ValueInfo rhs)
      : op(op), assign_to(assign_to), lhs(lhs), rhs(rhs) {}
  
  void dump() const { llvm::errs() << toString() << "\n"; }
  std::string toString() const {
    if (op == "/z3|=/") {
      return assign_to.name + " |= " + lhs.name + " << " + rhs.name + ";";
    }
    if (op == "/z3=/") {
      return assign_to.name + " = " + lhs.name + " & (1 << " + rhs.name + ")" +
             "; // alias";
    }
    if (op == "=") {
      return assign_to.name + " = " + lhs.name + ";";
    }
    if (op == "//") {
      return op + assign_to.name;
    }
    if (rhs.isNone()) {
      // Unary op
      return assign_to.name + " = " + op + lhs.name + ";";
    }
    return assign_to.name + " = " + lhs.name + op + rhs.name + ";";
  }

public:
  std::string op;
  ValueInfo assign_to, lhs, rhs;
};
class Region {
public:
  Region() {}

  size_t count() const { return instructions.size(); }
  static const Region end() { return Region(); }
  bool isEnd() { return (count() == 0); }

  void dump() const {
    llvm::errs() << "Region Debug Info:\n";

    // Dump instructions
    llvm::errs() << "Instructions:\n";
    // for (const auto &inst : instructions) {
    //   llvm::errs() << "  Op: " << inst.op << "\n";
    //   llvm::errs() << "    AssignTo: " << valueInfoToString(inst.assign_to)
    //                << "\n";
    //   llvm::errs() << "    LHS: " << valueInfoToString(inst.lhs) << "\n";
    //   llvm::errs() << "    RHS: " << valueInfoToString(inst.rhs) << "\n";
    // }

    // Simplified
    for (const auto &inst : instructions) {
      inst.dump();
    }

    // Dump symbol table
    llvm::errs() << "Symbol Table:\n";
    for (const auto &entry : st) {
      const auto &name = entry.first;
      const auto &valueInfo = entry.second;
      llvm::errs() << "  " << name << ": " << valueInfoToString(valueInfo)
                   << "\n";
    }

    // Dump the output mapping
    // llvm::errs() << "Output Mapping:\n";
    // for (const auto &pair : outputs2xored) {
    //   llvm::errs() << "OVar: " << pair.first;
    //   const auto &xor_set = pair.second;
    //   for (auto xor_var : xor_set) {
    //     llvm::errs() << xor_var.name << ",";
    //   }
    //   llvm::errs() << "\n";
    // }

    llvm::errs() << "Total Instructions: " << count() << "\n";
  }

private:
  std::string valueInfoToString(const ValueInfo &val) const {
    std::ostringstream oss;
    oss << "{Name: " << val.name << ", Width: " << val.width
        << ", Prop: " << toString(val.prop)
        << ", ClangDecl: " << (val.clangDecl ? "Valid" : "Null") << "}";
    if (val.clangDecl) {
      val.clangDecl->dump();
    }
    return oss.str();
  }

public:
  std::vector<Instruction> instructions;
  // TODO: make it private
  SymbolTable st;
};

static Region getNullRegion() { return Region(); }

class Pass {
public:
  virtual Region get() = 0;
};

inline size_t getWidthFromType(std::string_view type) {
  size_t width = 1;
  if (type.find("uint2") != std::string::npos) {
    width = 2;
  } else if (type.find("uint8") != std::string::npos) {
    width = 8;
  } else if (type.find("uint16") != std::string::npos) {
    width = 16;
  } else if (type.find("uint32") != std::string::npos) {
    width = 32;
  } else if (type.find("uint64") != std::string::npos) {
    width = 64;
  } else if (type.find("int8") != std::string::npos) {
    width = -8;
  } else if (type.find("int16") != std::string::npos) {
    width = -16;
  } else if (type.find("int32") != std::string::npos) {
    width = -32;
  } else if (type.find("int64") != std::string::npos) {
    width = -64;
  }
  return width;
}

// FIXME: we should flatten the chain by default (similar to find-union-set)
template <typename Key>
inline Key find_root(const std::unordered_map<Key,Key>& index, const Key& key) {
  Key result = key;
  while (index.count(result) && index.at(result)!=result) {
    result = index.at(result);
  }
  return result;
}