#pragma once

#include <clang/AST/Decl.h>

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
    ValueInfo(std::string_view name, Width width, VProp prop, const clang::VarDecl *clangDecl)
        : name(name), width(width), prop(prop), clangDecl(clangDecl) {}
    ValueInfo(ValueInfo &&val) noexcept : name(val.name), width(val.width), prop(val.prop), clangDecl(val.clangDecl) {}
    ValueInfo(const ValueInfo &val) : name(val.name), width(val.width), prop(val.prop), clangDecl(val.clangDecl) {}

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
        return name == other.name && clangDecl == other.clangDecl && width == other.width && prop == other.prop;
    }

    bool operator!=(const ValueInfo &other) const { return !(*this == other); }

    /// TODO: accept a SymbolTable ref to update
    static ValueInfo getNewRand() {
        const size_t ID_START = 10;
        static std::atomic<size_t> currentId{ID_START};

        std::string newName = "r" + std::to_string(currentId++);
        return ValueInfo(newName, 1, VProp::RND, nullptr);
    }

    bool isNone() const { return width == 0 && prop == VProp::UNK && clangDecl == nullptr; }

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
template <>
struct hash<ValueInfo> {
    std::size_t operator()(const ValueInfo &v) const {
        if (!v.clangDecl) {
            return v.name.length();
        }
        return v.clangDecl->getGlobalID();
    }
};
}  // namespace std

using SymbolTable = std::unordered_map<std::string, ValueInfo>;

class Instruction {
public:
    Instruction(std::string_view op, std::string_view content)
        : op(op),
          res(ValueInfo{content, 0, VProp::PUB, nullptr}),
          lhs(ValueInfo{}),
          rhs(ValueInfo{}) {}  // TODO: check if a binary op lacks the right hand side operand

    Instruction() = default;
    Instruction(const Instruction &inst) noexcept : op(inst.op), res(inst.res), lhs(inst.lhs), rhs(inst.rhs) {}
    Instruction(Instruction &&inst) noexcept
        : op(std::move(inst.op)), res(std::move(inst.res)), lhs(std::move(inst.lhs)), rhs(std::move(inst.rhs)) {}
    Instruction &operator=(const Instruction &inst) noexcept {
        if (this != &inst) {
            op = inst.op;
            res = inst.res;
            lhs = inst.lhs;
            rhs = inst.rhs;
        }
        return *this;
    }
    Instruction &operator=(Instruction &&inst) noexcept {
        if (this != &inst) {
            op = std::move(inst.op);
            res = std::move(inst.res);
            lhs = std::move(inst.lhs);
            rhs = std::move(inst.rhs);
        }
        return *this;
    }

    ~Instruction() noexcept = default;

    Instruction(std::string_view op, ValueInfo res, ValueInfo lhs, ValueInfo rhs)
        : op(op), res(res), lhs(lhs), rhs(rhs) {}

    void dump() const { llvm::errs() << toString() << "\n"; }
    inline std::string toString() const {
        if (op == "/z3|=/") {
            return res.name + " |= " + lhs.name + " << " + rhs.name + ";";
        }
        if (op == "/z3=/") {
            return res.name + " = " + lhs.name + " & (1 << " + rhs.name + ")" + "; // alias";
        }
        if (op == "=") {
            return res.name + " = " + lhs.name + ";";
        }
        if (op == "//") {
            return op + res.name;
        }
        if (isUnaryOp()) {
            // Unary op
            return res.name + " = " + op + lhs.name + ";";
        }
        return res.name + " = " + lhs.name + op + rhs.name + ";";
    }

    inline bool isUnaryOp() const { return rhs.isNone(); }

public:
    std::string op;
    ValueInfo res, lhs, rhs;
};

class Region {
public:
    Region() = default;
    Region(const Region &o) : sym_tbl(o.sym_tbl) {}
    Region(Region &&o) : sym_tbl(std::move(o.sym_tbl)) {}
    Region &operator=(const Region &o) {
        if (this != &o) {
            insts = o.insts;
            sym_tbl = o.sym_tbl;
        }
        return *this;
    }
    Region &operator=(Region &&o) {
        if (this != &o) {
            insts = std::move(o.insts);
            sym_tbl = std::move(o.sym_tbl);
        }
        return *this;
    }

    Region(const SymbolTable &st) : sym_tbl(st) {}
    Region(SymbolTable &&st) : sym_tbl(std::move(st)) {}

    size_t count() const { return insts.size(); }
    static const Region end() { return Region(); }
    bool isEnd() { return (count() == 0); }

    inline void dump() const {
        llvm::errs() << "Region Debug Info:\n";

        // Dump instructions
        llvm::errs() << "Instructions:\n";
        // for (const auto &inst : instructions) {
        //   llvm::errs() << "  Op: " << inst.op << "\n";
        //   llvm::errs() << "    AssignTo: " << valueInfoToString(inst.res)
        //                << "\n";
        //   llvm::errs() << "    LHS: " << valueInfoToString(inst.lhs) << "\n";
        //   llvm::errs() << "    RHS: " << valueInfoToString(inst.rhs) << "\n";
        // }

        // Simplified
        for (const auto &inst : insts) {
            inst.dump();
        }

        // Dump symbol table
        llvm::errs() << "Symbol Table:\n";
        for (const auto &entry : sym_tbl) {
            const auto &name = entry.first;
            const auto &valueInfo = entry.second;
            llvm::errs() << "  " << name << ": " << valueInfoToString(valueInfo) << "\n";
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

    inline static Region getNullRegion() { return Region(); }

private:
    std::string valueInfoToString(const ValueInfo &val) const {
        std::ostringstream oss;
        oss << "{Name: " << val.name << ", Width: " << val.width << ", Prop: " << toString(val.prop)
            << ", ClangDecl: " << (val.clangDecl ? "Valid" : "Null") << "}";
        if (val.clangDecl) {
            val.clangDecl->dump();
        }
        return oss.str();
    }

public:
    std::vector<Instruction> insts;
    SymbolTable sym_tbl;
};

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

const auto &find_root(auto &index, const auto &key) {
    auto current = key;
    while (true) {
        auto it = index.find(current);
        if (it == index.end() || it->second == current) break;
        current = it->second;
    }

    const auto &root = current;
    current = key;
    while (true) {
        auto it = index.find(current);
        if (it == index.end() || it->second == root) break;
        auto parent = it->second;
        it->second = root;
        current = parent;
    }

    return root;
}

// Mixin classes

template <class T>
class NonCopyable {
public:
    NonCopyable(const NonCopyable &) = delete;
    T &operator=(const T &) = delete;

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;  /// Protected non-virtual destructor
};
