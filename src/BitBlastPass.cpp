#include "Re-Sc-Masker/BitBlastPass.hpp"

#include <llvm-16/llvm/Support/raw_ostream.h>
#include <z3++.h>

#include <cassert>
#include <optional>
#include <string>
#include <utility>

#include "Re-Sc-Masker/Preludes.hpp"

std::uint32_t Z3VInfo::max_topo_id = 0;

// FIXME: For multiple regions, we currently re-assemble bits of a temporary
// variable before passing it to the next region. This will cause
// redundant assembling.
// (but it is a good start)
BitBlastPass::BitBlastPass(const ValueInfo &ret, Region &&origin_region) {
    auto &st = origin_region.sym_tbl;
    blasted_region.sym_tbl = st;

    // Calculate topo sort id of each var,
    // this process is essential when transforming an equivalence in SMT solver
    // (e.g. `a==b`) into an assignment `a=b` or `b=a` (the one with smaller topo sort id should be RHS).
    calc_topo(origin_region);

    // TODO: blast var to bits for fparams.

    llvm::errs() << "===BitBlastPass: started===\n";
    // Iterate over all vars to register bits in all variables
    // first: name; second: var (ValueInfo)
    for (const auto &var : st) {
        const std::string &var_name = var.first;
        const ValueInfo &var_info = var.second;
        z3::expr z3bv = z3ctx.bv_const(var_name.c_str(), var_info.width);

        // register the corresponding Z3 bitvec for the var
        var_expr.emplace(var_info, z3bv);

        // we have #width bits for each var
        var_bits[var_info].resize(var_info.width);

        // now create each bit in Z3 (var_name#i)
        for (auto i = 0; i < var_info.width; i++) {
            std::string var_bit_name = var_name + "#" + std::to_string(i);
            z3::expr bit_i = z3ctx.bool_const(var_bit_name.c_str());
            var_bits[var_info][i] = std::optional<z3::expr>(bit_i);
            // auto mask = z3ctx.bv_val((uint64_t(1) << i), var_info.width);
            // z3goal.add(bit_i == ((z3var & mask) == mask));
        }
    }

    // Register for the return value
    // TODO: we assume the the return value is a var here. In the future this may be an expr
    this->ret = ret;
    const std::string &var_name = ret.name;
    z3::expr z3var = z3ctx.bv_const(var_name.c_str(), ret.width);
    var_bits[ret].resize(ret.width);

    for (auto i = 0; i < ret.width; i++) {
        std::string var_bit_name = var_name + "#" + std::to_string(i);
        z3::expr bit_i = z3ctx.bool_const(var_bit_name.c_str());
        var_bits[ret][i] = std::optional<z3::expr>(bit_i);
        // auto mask = z3ctx.bv_val((uint64_t(1) << i), ret.width);
        // z3goal.add(bit_i == ((z3var & mask) == mask));
    }

    // TODO: we should also treat pointers in fparam as "output" values
    // ...

    // Set bit-blasting tactic
    z3::tactic simplify{z3ctx, "simplify"};

    z3::tactic t{z3ctx, "bit-blast"};
    z3::params p{z3ctx};
    p.set("blast_full", true);
    z3::tactic bit_blast = with(t, p);

    z3::tactic simplify_again{z3ctx, "simplify"};

    optimize_tactic = simplify & bit_blast & simplify_again;

    // Bit-blast each instructions
    for (auto &&inst : std::exchange(origin_region.insts, {})) {
        blast(std::move(inst));
    }
}

/// Calculate topo sort order id (tid) for each var.
/// All vars have tid=0 initially;
/// For each `C = A ^ B`, tid_C = 1 + max(tid_A, tid_B)
/// For each `X = Y`, tid_X = tid_Y + 1
/// This is essentially a simplified way to encode data dependencies,
/// and it works because of the linearity of crypto programs
void BitBlastPass::calc_topo(const Region &r) {
    for (const auto &inst : r.insts) {
        if (inst.isUnaryOp()) {
            var_topo[inst.res] = var_topo[inst.lhs] + 1;
        } else {
            auto ltopo = var_topo[inst.lhs];
            auto rtopo = var_topo[inst.rhs];
            var_topo[inst.res] = std::max(ltopo, rtopo) + 1;
        }
        llvm::errs() << inst.res.name << ": tid=" << var_topo[inst.res] << "\n";
    }
}

/// Return a bit-blasted version of the instruction
void BitBlastPass::blast(Instruction &&inst) {
    // TODO: Add the constraints from instructions
    inst.dump();

    z3::goal goal{z3ctx};
    if (inst.op == "=") {
        // Assign operation: a = b
        auto target_expr = var_expr[inst.res].value();
        auto left_expr = var_expr[inst.lhs].value();
        goal.add(target_expr == left_expr);
    } else if (inst.op == "^") {
        auto left_expr = var_expr[inst.lhs].value();
        auto right_expr = var_expr[inst.rhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (left_expr ^ right_expr));
    } else if (inst.op == "|") {
        auto left_expr = var_expr[inst.lhs].value();
        auto right_expr = var_expr[inst.rhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (left_expr | right_expr));
    } else if (inst.op == "&") {
        auto left_expr = var_expr[inst.lhs].value();
        auto right_expr = var_expr[inst.rhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (left_expr & right_expr));
    } else if (inst.op == "~") {
        auto left_expr = var_expr[inst.lhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (~left_expr));
    } else if (inst.op == "*") {
        auto left_expr = var_expr[inst.lhs].value();
        auto right_expr = var_expr[inst.rhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (left_expr * right_expr));
    } else if (inst.op == "+") {
        auto left_expr = var_expr[inst.lhs].value();
        auto right_expr = var_expr[inst.rhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (left_expr + right_expr));
    } else if (inst.op == "-") {
        auto left_expr = var_expr[inst.lhs].value();
        auto right_expr = var_expr[inst.rhs].value();
        auto target_expr = var_expr[inst.res].value();
        goal.add(target_expr == (left_expr - right_expr));
    } else if (inst.op == "!") {
        llvm::errs() << "Not implemented: " << inst.op << "\n";
    } else {
        llvm::errs() << "Not implemented: " << inst.op << "\n";
    }

    solve_and_extract(goal);
}

Z3VInfo BitBlastPass::traverseZ3Model(const z3::expr &e, int depth) {
    for (auto i = 0; i < depth * 4; i++) {
        llvm::errs() << (i % 4 ? "-" : "|");
    }

    if (e.is_var()) {
        llvm::errs() << "var: " << e.to_string() << "\n";
        return Z3VInfo(e.to_string(), Z3VType::Other);  // FIXME: May be input/output
    } else if (e.is_const()) {
        llvm::errs() << "const: " << e.to_string() << "\n";
        return Z3VInfo(e.to_string(), Z3VType::Other);  // FIXME: May be input/output
    } else if (e.is_not()) {
        llvm::errs() << "~\n";
        // auto width = e.arg(0).get_sort().bv_size();
        Width width = 1;

        auto oprand = traverseZ3Model(e.arg(0), depth + 1);

        if (depth == 1) {  // Rewrite `a = (v==expr / expr == v)` to `tmp=expr; v=!tmp;`
            auto lhs = blasted_region.insts.back().lhs;
            auto rhs = blasted_region.insts.back().rhs;
            auto new_var_name = Z3VInfo::getNewName();
            auto new_var = ValueInfo{new_var_name, width, VProp::UNK, nullptr};
            blasted_region.sym_tbl[new_var_name] = new_var;
            // FIXME: We need to know which side should be the expr, and which side
            // should be the v
            auto origin_var = blasted_region.insts.back().rhs;
            blasted_region.insts.back() = Instruction{"=", new_var, blasted_region.insts.back().lhs, ValueInfo{}};
            blasted_region.insts.emplace_back(Instruction{"!", origin_var, new_var, ValueInfo{}});
            return Z3VInfo(new_var_name, Z3VType::Other);
        }

        std::string temp_name = Z3VInfo::getNewName();
        auto new_var = ValueInfo{temp_name, width, VProp::UNK, nullptr};
        blasted_region.sym_tbl[temp_name] = new_var;
        // Use `!` instead of `~` for boolean variables!
        llvm::errs() << "New inst. depth:" << depth << " op~!\n";
        blasted_region.insts.emplace_back(Instruction{width == 1 ? "!" : "~", new_var,
                                                      ValueInfo{oprand.name, width, VProp::UNK, nullptr}, ValueInfo{}});

        return Z3VInfo(temp_name, Z3VType::Other);
    } else if (e.is_app()) {
        auto decl = e.decl();
        std::string name;

        if (decl.name().kind() == Z3_STRING_SYMBOL) {
            name = decl.name().str();
        } else {
            name = "non-string-symbol";
        }

        // handle aliases
        if (name == "=" && depth == 1 && e.num_args() == 2 &&
            (e.arg(0).is_const() || e.arg(1).is_const())) {  // This is an potential aliasing!
            if (e.arg(0).is_const() && e.arg(0).decl().name().kind() == Z3_STRING_SYMBOL) {  // rhs
                auto alias_id = e.arg(1).decl().name().to_int();
                auto alias_name = "k!" + std::to_string(alias_id);
                auto origin_name = e.arg(0).decl().name().str();
                auto origin_var = origin_name.substr(0, origin_name.find('#'));
                auto origin_prop = blasted_region.sym_tbl[origin_var].prop;

                alias_of[alias_id] = origin_name;
                z3alias[origin_name] = alias_id;
                llvm::errs() << "alias_of[" << alias_id << "] = " << origin_name << "\n";
                llvm::errs() << alias_id << " (alias)\n";

                // The property of the alias is the same as the origin variable
                blasted_region.sym_tbl[alias_name] = ValueInfo{alias_name, 1, origin_prop, nullptr};
                return Z3VInfo{"!ALIAS", Z3VType::Other};
            }
            if (e.arg(1).is_const() && e.arg(1).decl().name().kind() == Z3_STRING_SYMBOL) {  // lhs
                auto alias_id = e.arg(0).decl().name().to_int();
                auto alias_name = "k!" + std::to_string(alias_id);
                auto origin_name = e.arg(1).decl().name().str();
                auto origin_var = origin_name.substr(0, origin_name.find('#'));
                auto origin_prop = blasted_region.sym_tbl[origin_var].prop;

                alias_of[alias_id] = origin_name;
                z3alias[origin_name] = alias_id;
                llvm::errs() << "alias_of[" << alias_id << "] = " << origin_name << "\n";
                llvm::errs() << alias_id << " (alias)\n";
                blasted_region.sym_tbl[alias_name] = ValueInfo{alias_name, 1, origin_prop, nullptr};
                return Z3VInfo{"!ALIAS", Z3VType::Other};
            }
        }

        llvm::errs() << "app: " << name << "\n";
        Z3VInfo prev;
        if (name == "=" || name == "and" || name == "or") {
            std::string opname;
            if (name == "=") {
                opname = "==";
            } else if (name == "and") {
                opname = "&";
            } else if (name == "or") {
                opname = "|";
            } else {
                opname = "UNKNOWN_OP";
            }
            if (name == "and" && depth == 0) {
                for (unsigned i = 0; i < e.num_args(); i++) {
                    traverseZ3Model(e.arg(i), depth + 1);
                }
                return Z3VInfo();
            }

            // Replace all top-layer `==` expressions
            // e.g.:
            // lhs==rhs -> lhs=rhs / rhs=lhs
            // !(lhs==rhs) => lhs=!rhs / rhs!=lhs
            // FIXME: we need to know which side shold be the operand exactly
            if (depth == 1 && name == "=") {
                assert(e.num_args() == 2 && "Wrong arg count for top-level `==` op.");
                auto lhs = traverseZ3Model(e.arg(0), depth + 1);
                auto rhs = traverseZ3Model(e.arg(1), depth + 1);
                auto temp_name = Z3VInfo::getNewName();
                Width width = 1;

                // Determine assignment direction based on variable properties
                bool lhs_in_st = blasted_region.sym_tbl.find(lhs.name) != blasted_region.sym_tbl.end();
                bool rhs_in_st = blasted_region.sym_tbl.find(rhs.name) != blasted_region.sym_tbl.end();
                auto lhs_prop = blasted_region.sym_tbl[lhs.name].prop;
                auto rhs_prop = blasted_region.sym_tbl[rhs.name].prop;

                if (lhs_prop == VProp::RND || lhs_prop == VProp::SECRET || lhs_prop == VProp::PUB) {
                    // If LHS is input type, RHS should be assigned LHS value
                    blasted_region.insts.emplace_back(Instruction{"=", ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{}});
                    return Z3VInfo(temp_name, Z3VType::Other);

                } else if (rhs_prop == VProp::RND || rhs_prop == VProp::SECRET || rhs_prop == VProp::PUB) {
                    // If LHS not defined but RHS exists, assign RHS to LHS
                    blasted_region.insts.emplace_back(Instruction{"=", ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{}});
                    return Z3VInfo(temp_name, Z3VType::Other);
                }

                // If we reach here, we can't determine the correct assignment direction
                llvm::errs() << "Warning: Cannot determine assignment direction for " << lhs.name << " == " << rhs.name
                             << "\n";

                // Check which side has not been defined
                if (lhs_in_st) {
                    blasted_region.insts.emplace_back(Instruction{"=", ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{}});
                } else {
                    blasted_region.insts.emplace_back(Instruction{"=", ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{}});
                }
                return Z3VInfo(temp_name, Z3VType::Other);
            }

            for (unsigned i = 0; i < e.num_args(); i++) {
                auto child = traverseZ3Model(e.arg(i), depth + 1);
                if (i == 0) {
                    prev = std::move(child);
                } else {
                    llvm::errs() << "New name: " << opname << "\n";
                    auto temp_name = Z3VInfo::getNewName();
                    //   auto width = e.arg(i).get_sort().bv_size();
                    Width width = 1;

                    auto new_var = ValueInfo{temp_name, width, VProp::UNK, nullptr};
                    blasted_region.sym_tbl[temp_name] = new_var;
                    llvm::errs() << "New inst. depth:" << depth << " op" << opname << "\n";
                    blasted_region.insts.emplace_back(Instruction{opname, new_var,
                                                                  ValueInfo{prev.name, width, VProp::UNK, nullptr},
                                                                  ValueInfo{child.name, width, VProp::UNK, nullptr}});
                    prev = Z3VInfo(temp_name, Z3VType::Other);
                    llvm::errs() << "prev: " << prev.name << "+=" << child.name << "\n";
                }
            }
            llvm::errs() << "prev: " << prev.name << "\n";
            return prev;
        }
        if (name == "if") {
            // (ite k!7 (not (= k!5 k!4)) k!5)
            // then_expr: (not (= k!5 k!4))
            // else_expr: k!5
            // cond_expr: k!7
            // result = (then & !cond) | (else & cond)
            auto cond_z3 = traverseZ3Model(e.arg(0), depth + 1);
            auto then_z3 = traverseZ3Model(e.arg(1), depth + 1);
            auto else_z3 = traverseZ3Model(e.arg(2), depth + 1);
            const Width width = 1;
            auto then_expr_name = Z3VInfo::getNewName();
            auto else_expr_name = Z3VInfo::getNewName();
            auto ncond_expr_name = Z3VInfo::getNewName();       // i.e. !cond
            auto result_name = Z3VInfo::getNewName() + "_ite";  // i.e. !cond
            auto then_expr = ValueInfo{then_expr_name, width, VProp::UNK, nullptr};
            blasted_region.sym_tbl[then_expr_name] = then_expr;
            auto else_expr = ValueInfo{else_expr_name, width, VProp::UNK, nullptr};
            blasted_region.sym_tbl[else_expr_name] = else_expr;
            auto ncond_expr = ValueInfo{ncond_expr_name, width, VProp::UNK, nullptr};
            blasted_region.sym_tbl[ncond_expr_name] = ncond_expr;
            auto result_expr = ValueInfo{result_name, width, VProp::UNK, nullptr};
            blasted_region.sym_tbl[result_name] = result_expr;

            blasted_region.insts.emplace_back(
                Instruction{"!", ncond_expr, ValueInfo{cond_z3.name, width, VProp::UNK, nullptr}, ValueInfo{}});
            blasted_region.insts.emplace_back(Instruction{"&", then_expr,
                                                          ValueInfo{then_z3.name, width, VProp::UNK, nullptr},
                                                          ValueInfo{cond_z3.name, 1, VProp::UNK, nullptr}});
            blasted_region.insts.emplace_back(Instruction{"&", else_expr,
                                                          ValueInfo{else_z3.name, width, VProp::UNK, nullptr},
                                                          ValueInfo{ncond_expr.name, 1, VProp::UNK, nullptr}});
            blasted_region.insts.emplace_back(Instruction{"|", result_expr, then_expr, else_expr});
            return Z3VInfo(result_name, Z3VType::Other);

        } else {
            llvm::errs() << "Unknown app: " << name << "\n";
        }
    } else if (e.is_quantifier()) {
        llvm::errs() << "quantifier: " << e.to_string() << "\n";
    } else {
        llvm::errs() << "UNKNOWN node: " << e.to_string() << "\n";
    }
    return Z3VInfo{};
}
void BitBlastPass::solve_and_extract(const z3::goal &goal) {
    if (!optimize_tactic) {
        llvm::errs() << "Error: No tactic set\n";
        exit(1);
    }
    z3::apply_result result = (*optimize_tactic)(goal);

    llvm::errs() << "------------------\n";

    llvm::errs() << "- Simplified and bit-blasted constraints:\n";
    for (unsigned i = 0; i < result.size(); ++i) {
        llvm::errs() << result[i].as_expr().to_string() << "\n";
        traverseZ3Model(result[i].as_expr());
    }

    // Dump z3alias
    llvm::errs() << "- z3alias:\n";
    for (const auto &alias : z3alias) {
        llvm::errs() << alias.first << " -> " << alias.second << "\n";
    }

    llvm::errs() << "------------------\n";
}

Region BitBlastPass::get() {
    // Extract input variable bits at the beginning of the function body
    std::vector<Instruction> temp_instructions;
    llvm::errs() << "inserting input bits:\n";
    for (const auto &var_bit : var_bits) {
        if (var_bit.first.prop == VProp::PUB || var_bit.first.prop == VProp::SECRET) {  // Only for input variables
            llvm::errs() << "inserting input bits for " << var_bit.first.name << "\n";
            for (unsigned i = 0; i < var_bit.second.size(); ++i) {
                auto var_bit_name = var_bit.first.name + "#" + std::to_string(i);
                assert(var_bit.second[i].has_value() && "empty bit");
                temp_instructions.emplace_back(Instruction{
                    "/z3=/", ValueInfo{"k!" + std::to_string(z3alias[var_bit_name]), 1, VProp::CST, nullptr},
                    var_bit.first, ValueInfo{std::to_string(i), 1, VProp::CST, nullptr}});
            }
        }
    }
    blasted_region.insts.insert(blasted_region.insts.begin(), temp_instructions.begin(), temp_instructions.end());

    // Assemble output vars from bits at the end of the function body
    for (const auto &var_bit : var_bits) {
        if (var_bit.first.prop == VProp::OUTPUT) {
            blasted_region.insts.emplace_back(
                Instruction{"=", var_bit.first, ValueInfo{"0", 1, VProp::CST, nullptr}, ValueInfo{}});
            for (unsigned i = 0; i < var_bit.second.size(); ++i) {
                if (var_bit.second[i].has_value()) {
                    auto var_bit_name = var_bit.first.name + "#" + std::to_string(i);
                    blasted_region.insts.emplace_back(
                        Instruction{"/z3|=/", var_bit.first,
                                    ValueInfo{"k!" + std::to_string(z3alias[var_bit_name]), 1, VProp::CST, nullptr},
                                    ValueInfo{std::to_string(i), 1, VProp::CST, nullptr}});
                }
            }
        }
    }

    llvm::errs() << "blasted region:\n";
    blasted_region.dump();

    return blasted_region;
}