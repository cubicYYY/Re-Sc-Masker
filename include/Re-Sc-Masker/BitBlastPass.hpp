#pragma once

#include <z3++.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"

class Z3VInfo;

struct BitBlastPass {
    virtual Region get() = 0;
};

/// Utilizes Z3 Solver to eliminate integer operations
class Z3BitBlastPass : public BitBlastPass, private NonCopyable<Z3BitBlastPass> {
public:
    using TopoId = std::uint32_t;
    /// Encode relationship between separated bits in Z3
    explicit Z3BitBlastPass(const ValueInfo &ret, Region &&origin_region);

    /// Return the bit-blasted region
    Region get() override;

private:
    void calc_topo(const Region &origin_region);
    /// Encode relationship between separated bits in Z3
    void blast(Instruction &&inst);
    /// Traverse the model to get the representation of output variables
    Z3VInfo traverseZ3Model(const z3::expr &e, int indent = 0);
    void solve_and_extract(const z3::goal &goal);
    void splitVar2Bits(const ValueInfo &var);

private:
    z3::context z3ctx;

    /// var name -> topo sort id
    std::unordered_map<std::string, TopoId> var2topo{};

    /// z3varbit id -> topo sort id
    std::unordered_map<int, TopoId> id2topo{};

    /// var -> z3 bit vector
    std::unordered_map<ValueInfo, std::optional<z3::expr>> var2bitvec{};

    /// var -> z3 bits for each bit
    std::unordered_map<ValueInfo, std::vector<std::optional<z3::expr>>> var2bits{};

    /// var -> z3 mask goals
    std::unordered_map<ValueInfo, std::vector<z3::expr>> var2masks{};

    /// e.g. `k!4` -> `|out#3|`
    std::unordered_map<int, std::string> id2varbit;
    /// e.g. `|out#3|` -> `k!4`
    std::unordered_map<std::string, int> varbit2id;

    std::unordered_set<ValueInfo> var_splited;

    Region blasted_region;
    ValueInfo ret;
};

enum class Z3VType { Other, Input, Output };

class Z3VInfo {
public:
    Z3VInfo() : name(""), type(Z3VType::Other), topo_id(0) {}

    Z3VInfo(std::string_view name, Z3VType type, Z3BitBlastPass::TopoId topo_id = 0)
        : name(name), type(type), topo_id(topo_id) {}

    Z3VInfo(const Z3VInfo &o) : name(o.name), type(o.type), topo_id(0) {}

    Z3VInfo(Z3VInfo &&o) noexcept : name(std::move(o.name)), type(o.type), topo_id(o.topo_id) {}

    Z3VInfo &operator=(Z3VInfo &&o) noexcept {
        if (this != &o) {
            name = std::move(o.name);
            type = o.type;
            topo_id = o.topo_id;
        }
        return *this;
    }

    ~Z3VInfo() = default;

    static std::string getNewName() {
        static int id = 0;
        return "z3_" + std::to_string(id++);
    }

    static Z3BitBlastPass::TopoId getNewTopoId() { return ++max_topo_id; }

    static void updateMaxTopoId(Z3BitBlastPass::TopoId new_topo) { max_topo_id = std::max(max_topo_id, new_topo); }

    static Z3BitBlastPass::TopoId max_topo_id;

public:
    std::string name;
    Z3VType type;
    Z3BitBlastPass::TopoId topo_id;
};