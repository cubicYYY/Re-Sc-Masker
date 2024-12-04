#pragma once

#include "clang/AST/Decl.h"
#include <atomic>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Variable property
enum class VProp {
  UNK,
  MASKED,
  PUB,
  RND,
  CST,
  SECRET,
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
  default:
    return "Unknown";
  }
}

// TODO: maybe reuse LLVM ones?
enum class VType {
  UNK,
  Bool,
};

/// TODO: auto insertion into an optional SymbolTable
class ValueInfo {
public:
  ValueInfo()
      : name(""), type(VType::UNK), prop(VProp::UNK), clangDecl(nullptr) {}
  ValueInfo(std::string_view name, VType type, VProp prop,
            const clang::VarDecl *clangDecl)
      : name(name), type(type), prop(prop), clangDecl(clangDecl) {}
  ValueInfo(ValueInfo &&val)
      : name(val.name), type(val.type), prop(val.prop),
        clangDecl(val.clangDecl) {}
  ValueInfo(const ValueInfo &val)
      : name(val.name), type(val.type), prop(val.prop),
        clangDecl(val.clangDecl) {}

  // Copy Assignment Operator
  ValueInfo &operator=(const ValueInfo &other) {
    name = other.name;
    type = other.type;
    prop = other.prop;
    clangDecl = other.clangDecl;
    return *this;
  }

  // Move Assignment Operator
  ValueInfo &operator=(ValueInfo &&other) noexcept {
    name = other.name;
    type = other.type;
    prop = other.prop;
    clangDecl = other.clangDecl;
    return *this;
  }

  bool operator==(const ValueInfo &other) const {
    return name == other.name && clangDecl == other.clangDecl &&
           type == other.type && prop == other.prop;
  }

  bool operator!=(const ValueInfo &other) const { return !(*this == other); }

  /// TODO: accept a SymbolTable ref to update
  static ValueInfo getNewRand() {
    const size_t ID_START = 100;
    static std::atomic<size_t> currentId{ID_START};

    std::string newName = "r" + std::to_string(currentId++);
    return ValueInfo(newName, VType::Bool, VProp::RND, nullptr);
  }

  bool isNone() const {
    return type == VType::UNK && prop == VProp::UNK && clangDecl == nullptr;
  }

public:
  std::string name;
  VType type;
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
      return 0;
    }
    return v.clangDecl->getGlobalID();
  }
};
} // namespace std

using SymbolTable = std::unordered_map<std::string, ValueInfo>;

class Instruction {
public:
  Instruction() {}
  Instruction(std::string_view op, ValueInfo assignTo, ValueInfo lhs,
              ValueInfo rhs)
      : op(op), assignTo(assignTo), lhs(lhs), rhs(rhs) {}
  std::string toString() const {
    if (rhs.isNone()) {
      // Unary op
      return assignTo.name + " = " + op + lhs.name + ";";
    }
    return assignTo.name + " = " + lhs.name + op + rhs.name + ";";
  }

public:
  std::string op;
  ValueInfo assignTo, lhs, rhs;
};
class Region {
public:
  Region() {}

  void append(Instruction inst) { instructions.push_back(inst); }
  size_t count() const { return instructions.size(); }
  static const Region end() { return Region(); }
  bool isEnd() { return (count() == 0); }

  void dump() const {
    llvm::errs() << "Region Debug Info:\n";

    // Dump instructions
    llvm::errs() << "Instructions:\n";
    for (const auto &inst : instructions) {
      llvm::errs() << "  Op: " << inst.op << "\n";
      llvm::errs() << "    AssignTo: " << valueInfoToString(inst.assignTo)
                   << "\n";
      llvm::errs() << "    LHS: " << valueInfoToString(inst.lhs) << "\n";
      llvm::errs() << "    RHS: " << valueInfoToString(inst.rhs) << "\n";
    }

    // Dump outputs
    llvm::errs() << "Outputs:\n";
    for (const auto &output : outputs2xored) {
      llvm::errs() << "  " << output.first << "\n";
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
    llvm::errs() << "Output Mapping:\n";
    for (const auto &pair : outputs2xored) {
      llvm::errs() << "OVar: " << pair.first
                   << " -> XORed with: " << pair.second.name;
      llvm::errs() << "\n";
    }

    llvm::errs() << "Total Instructions: " << count() << "\n";
  }

private:
  std::string valueInfoToString(const ValueInfo &val) const {
    std::ostringstream oss;
    oss << "{Name: " << val.name
        << ", Type: " << (val.type == VType::Bool ? "Bool" : "Unknown")
        << ", Prop: " << toString(val.prop)
        << ", ClangDecl: " << (val.clangDecl ? "Valid" : "Null") << "}";
    if (val.clangDecl) {
      val.clangDecl->dump();
    }
    return oss.str();
  }

public:
  std::vector<Instruction> instructions;
  std::unordered_map<std::string, ValueInfo> outputs2xored;
  // TODO: make it private
  SymbolTable st;
};

static Region getNullRegion() { return Region(); }