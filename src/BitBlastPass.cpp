#include "Re-Sc-Masker/BitBlastPass.hpp"

#include <llvm-16/llvm/Support/raw_ostream.h>
#include <z3++.h>

#include <cassert>
#include <optional>
#include <string>
#include <utility>

#include "Re-Sc-Masker/Preludes.hpp"

std::uint32_t Z3VInfo::max_topo_id = 0;

Z3BitBlastPass::Z3BitBlastPass(const ValueInfo &ret, Region &&origin_region) {
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
    for (const auto &[var_name, var_info] : st) {
        z3::expr var_z3bv = z3ctx.bv_const(var_name.c_str(), var_info.width);

        // register the corresponding Z3 bitvec for the var
        var2bitvec.emplace(var_info, var_z3bv);

        // we have #width bits for each var
        var2bits[var_info].resize(var_info.width);

        // now create each bit in Z3 ("var_name#i")
        for (auto i = 0; i < var_info.width; i++) {
            std::string var_bit_name = var_name + "#" + std::to_string(i);
            z3::expr bit_i = z3ctx.bool_const(var_bit_name.c_str());
            auto mask = z3ctx.bv_val((uint64_t(1) << i), var_info.width);
            var2bits[var_info][i] = std::optional<z3::expr>(bit_i);
            var2masks[var_info].emplace_back(bit_i == ((var_z3bv & mask) == mask));
        }

        if (var_info.prop == VProp::PUB || var_info.prop == VProp::SECRET) {  // Only for input variables
            llvm::errs() << "inserting input bits for " << var_name << "\n";
            splitVar2Bits(var_info);
        }
    }

    // Register for the return value
    // TODO: we assume the the return value is a var here. In the future this may be an expr
    this->ret = ret;
    const std::string &var_name = ret.name;
    z3::expr z3var = z3ctx.bv_const(var_name.c_str(), ret.width);
    var2bits[ret].resize(ret.width);

    for (auto i = 0; i < ret.width; i++) {
        std::string var_bit_name = var_name + "#" + std::to_string(i);
        z3::expr bit_i = z3ctx.bool_const(var_bit_name.c_str());
        var2bits[ret][i] = std::optional<z3::expr>(bit_i);
    }

    // TODO: we should also treat pointers in fparam as "output" values
    // ...

    // Bit-blast each instructions
    for (auto &&inst : origin_region.insts) {
        varbit2id.clear();
        id2varbit.clear();
        blast(std::move(inst));
    }
}

/// Calculate topo sort order id (tid) for each var.
/// All vars have tid=0 initially;
/// For each `C = A ^ B`, tid_C = 1 + max(tid_A, tid_B)
/// For each `X = Y`, tid_X = tid_Y + 1
/// This is essentially a simplified way to encode data dependencies,
/// and it works because of the linearity of crypto programs
void Z3BitBlastPass::calc_topo(const Region &r) {
    for (const auto &inst : r.insts) {
        if (inst.isUnaryOp()) {
            var2topo[inst.res.name] = var2topo[inst.lhs.name] + 1;
        } else {
            auto ltopo = var2topo[inst.lhs.name];
            auto rtopo = var2topo[inst.rhs.name];
            var2topo[inst.res.name] = std::max(ltopo, rtopo) + 1;
        }
        llvm::errs() << inst.res.name << ": tid=" << var2topo[inst.res.name] << "\n";
    }
}

/// Bit-Blast a single instruction
void Z3BitBlastPass::blast(Instruction &&inst) {
    blasted_region.insts.emplace_back("//", inst.toString());
    inst.dump();

    z3::goal goal(z3ctx);
    for (const auto &mask : var2masks[inst.lhs]) {
        goal.add(mask);
    }
    for (const auto &mask : var2masks[inst.res]) {
        goal.add(mask);
    }
    if (!inst.isUnaryOp()) {
        for (const auto &mask : var2masks[inst.rhs]) {
            goal.add(mask);
        }
    }
    if (inst.op == "=") {
        // Assign operation: a = b
        auto target_expr = var2bitvec[inst.res].value();
        auto left_expr = var2bitvec[inst.lhs].value();

        goal.add(target_expr == left_expr);
    } else if (inst.op == "^") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto right_expr = var2bitvec[inst.rhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (left_expr ^ right_expr));
    } else if (inst.op == "|" || inst.op == "or") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto right_expr = var2bitvec[inst.rhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (left_expr | right_expr));
    } else if (inst.op == "&" || inst.op == "and") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto right_expr = var2bitvec[inst.rhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (left_expr & right_expr));
    } else if (inst.op == "~" || inst.op == "not") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (~left_expr));
    } else if (inst.op == "*") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto right_expr = var2bitvec[inst.rhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (left_expr * right_expr));
    } else if (inst.op == "+") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto right_expr = var2bitvec[inst.rhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (left_expr + right_expr));
    } else if (inst.op == "-") {
        auto left_expr = var2bitvec[inst.lhs].value();
        auto right_expr = var2bitvec[inst.rhs].value();
        auto target_expr = var2bitvec[inst.res].value();
        goal.add(target_expr == (left_expr - right_expr));
    } else if (inst.op == "!") {
        llvm::errs() << "Not implemented: " << inst.op << "\n";
    } else {
        llvm::errs() << "Not implemented: " << inst.op << "\n";
    }

    solve_and_extract(goal);
    if (var_splited.count(inst.res) && inst.res.prop == VProp::PUB ||
        inst.res.prop == VProp::SECRET) {  // first def only
        splitVar2Bits(inst.res);
    }
    // bits -> result
}

void Z3BitBlastPass::splitVar2Bits(const ValueInfo &var) {
    auto z3bits = var2bits[var];
    for (unsigned i = 0; i < z3bits.size(); ++i) {
        auto var_bit_name = var.name + "#" + std::to_string(i);
        blasted_region.insts.emplace_back(Instruction{"/var=>z3/", ValueInfo{var_bit_name, 1, VProp::CST, nullptr}, var,
                                                      ValueInfo{std::to_string(i), 1, VProp::CST, nullptr}});
    }
    var_splited.insert(var);
}

Z3VInfo Z3BitBlastPass::traverseZ3Model(const z3::expr &e, TraversingState state, int depth) {
    // utils
    auto name_to_z3id = [](std::string str) {  // e.g. "k!3" -> 3
        size_t pos = str.find('!');
        if (pos != std::string::npos && pos + 1 < str.size()) {
            int number = std::stoi(str.substr(pos + 1));
            return number;
        }
        return -1;

    };

    auto varbit_name2varname = [](std::string str) -> std::string {  // e.g. "out#3" -> out
        size_t pos = str.find('#');
        if (pos != std::string::npos) {
            return str.substr(0, pos);
        }
        return "<UNK>";

    };

    auto opname2operator = [](std::string opname, size_t width) -> std::string {
        if (opname == "=") {
            return "=";
        } else if (opname == "==") {
            return "==";
        } else if (opname == "!" || opname == "not") {
            return (width == 1 ? "!" : "~");
        } else if (opname == "||" || opname == "or") {
            return (width == 1 ? "||" : "|");
        } else if (opname == "&&" || opname == "and") {
            return (width == 1 ? "&&" : "&");
        } else if (opname == "^" || opname == "xor") {
            return "^";
        }
        return "<Unknown OP" + opname + ">";
    };

    for (auto i = 0; i < depth * 4; i++) {  // indents for debug output
        llvm::errs() << (i % 4 ? "-" : "|");
    }

    if (e.is_var()) {
        llvm::errs() << "var: " << e.to_string() << "\n";
        return Z3VInfo(e.to_string(), Z3VType::Other);
    } else if (e.is_const()) {  // a Z3 var
        llvm::errs() << "const: " << e.to_string() << "\n";
        auto potential_varbit = id2varbit.find(name_to_z3id(e.to_string()));
        if (potential_varbit != id2varbit.end()) {  // this Z3 var corresponds to a var in the region
            auto varbit_name = potential_varbit->second;
            llvm::errs() << "topo id: " << e.to_string() << " - " << var2topo[varbit_name2varname(varbit_name)] << "\n";
            return Z3VInfo(varbit_name, Z3VType::Other, var2topo[varbit_name2varname(varbit_name)]);
        }
        return Z3VInfo(e.to_string(), Z3VType::Other);
    } else if (e.is_not()) {
        llvm::errs() << "~\n";
        // auto width = e.arg(0).get_sort().bv_size();
        Width width = 1;

        auto oprand = traverseZ3Model(e.arg(0), state, depth + 1);

        if (depth == 1) {  //! top-level NOT: rewrite `not (v==expr)` to `v=!expr;`
            auto &last_inst = blasted_region.insts.back();
            assert(last_inst.op == "=" && "top-level NOT should always come after an equivalence (move-assignment)");
            last_inst.op = (width == 1 ? "!" : "~");
            return Z3VInfo(last_inst.res.name, Z3VType::Other);
        }

        std::string temp_name = Z3VInfo::getNewName();
        auto new_var = ValueInfo{temp_name, width, VProp::UNK, nullptr};
        blasted_region.sym_tbl[temp_name] = new_var;

        // NOTE: Use `!` instead of `~` for boolean variables!
        // `~bool_var` will always return true!
        llvm::errs() << "New inst. depth:" << depth << " op~!\n";
        blasted_region.insts.emplace_back(opname2operator("!", width), new_var,
                                          ValueInfo{oprand.name, width, VProp::UNK, nullptr}, ValueInfo{});

        return Z3VInfo(temp_name, Z3VType::Other);
    } else if (e.is_app()) {  // operators like "="
        auto decl = e.decl();
        std::string name;

        if (decl.name().kind() == Z3_STRING_SYMBOL) {
            name = decl.name().str();
        } else {
            name = "non-string-symbol";
        }

        // find: Z3 var -> var#bit
        if (name == "=" && depth == 1 && e.num_args() == 2 &&
            (e.arg(0).is_const() || e.arg(1).is_const())) {  // This is an potential aliasing!

            /// Z3 var internal id
            int alias_id;
            std::string varbit_name;
            if (e.arg(0).is_const() &&
                e.arg(0).decl().name().kind() == Z3_STRING_SYMBOL) {  // lhs is Z3 var, rhs is varbit
                alias_id = e.arg(1).decl().name().to_int();
                varbit_name = e.arg(0).decl().name().str();
            } else if (e.arg(1).is_const() &&
                       e.arg(1).decl().name().kind() == Z3_STRING_SYMBOL) {  // rhs is Z3 var. lhs is varbit
                alias_id = e.arg(0).decl().name().to_int();
                varbit_name = e.arg(1).decl().name().str();
            } else {
                alias_id = -1;
            }
            if (alias_id != -1) {  // corresponding var#bit found: then save this mapping
                auto origin_var = varbit_name.substr(0, varbit_name.find('#'));
                assert(blasted_region.sym_tbl.count(origin_var));
                auto origin_vinfo = blasted_region.sym_tbl[origin_var];

                id2varbit[alias_id] = varbit_name;
                blasted_region.insts.emplace_back("//", varbit_name + " -> " + std::to_string(alias_id));
                varbit2id[varbit_name] = alias_id;
                id2topo[alias_id] = var2topo[varbit_name];

                llvm::errs() << "id2varbit[" << alias_id << "] = " << varbit_name << "\n";

                // The property of the Z3 var is the same as the origin variable
                blasted_region.sym_tbl[varbit_name] = ValueInfo{varbit_name, 1, origin_vinfo.prop, nullptr};
                return Z3VInfo{"!ALIAS", Z3VType::Other};
            }
        }

        llvm::errs() << "app: " << name << "\n";
        Z3VInfo prev;
        if (name == "=" || name == "and" || name == "or") {
            if (name == "and" && depth == 0) {  // all expressions are undr a top level "and"
                for (unsigned i = 0; i < e.num_args(); i++) {
                    traverseZ3Model(e.arg(i), state, 1);
                }
                return Z3VInfo();
            }

            if (name == "=") {
                if (!(state & NEED_EXPRESSION)) {
                    // Replace `==` expressions corresponding to assignments
                    // e.g.:
                    // lhs==rhs -> lhs=rhs / rhs=lhs
                    // !(lhs==rhs) => lhs=!rhs / rhs!=lhs
                    assert(e.num_args() == 2 && "Wrong arg count for top-level `==` op.");
                    // No more assignments(statements) inside lower layers
                    auto lhs = traverseZ3Model(e.arg(0), state | NEED_EXPRESSION, depth + 1);
                    auto rhs = traverseZ3Model(e.arg(1), state | NEED_EXPRESSION, depth + 1);
                    auto temp_name = Z3VInfo::getNewName();
                    Width width = 1;

                    // Determine assignment direction based on variable properties
                    bool lhs_in_st = blasted_region.sym_tbl.find(lhs.name) != blasted_region.sym_tbl.end();
                    if (lhs_in_st) {
                        auto lhs_prop = blasted_region.sym_tbl[lhs.name].prop;
                        if (lhs_prop == VProp::RND || lhs_prop == VProp::SECRET ||
                            lhs_prop == VProp::PUB ||  // FIXME: add a "read-only" property?
                            lhs.topo_id < rhs.topo_id) {
                            // If LHS is input type, RHS should be assigned LHS value
                            blasted_region.insts.emplace_back("//", "(L)eq2assign: l=" + lhs.name + "." +
                                                                        std::to_string(lhs.topo_id) + " r=" + rhs.name +
                                                                        "." + std::to_string(rhs.topo_id));
                            blasted_region.insts.emplace_back("=", ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                              ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                              ValueInfo{});
                            return Z3VInfo(temp_name, Z3VType::Other);
                        }
                    }

                    bool rhs_in_st = (blasted_region.sym_tbl.find(rhs.name) != blasted_region.sym_tbl.end());
                    if (rhs_in_st) {
                        auto rhs_prop = blasted_region.sym_tbl[rhs.name].prop;
                        if (rhs_prop == VProp::RND || rhs_prop == VProp::SECRET || rhs_prop == VProp::PUB ||
                            lhs.topo_id > rhs.topo_id) {
                            // If LHS not defined but RHS exists, assign RHS to LHS
                            blasted_region.insts.emplace_back("//", "(R)eq2assign: l=" + lhs.name + "." +
                                                                        std::to_string(lhs.topo_id) + " r=" + rhs.name +
                                                                        "." + std::to_string(rhs.topo_id));
                            blasted_region.insts.emplace_back("=", ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                              ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                              ValueInfo{});
                            return Z3VInfo(temp_name, Z3VType::Other);
                        }
                    }

                    if (lhs.topo_id == rhs.topo_id) {
                        // If we reach here, we can't determine the correct assignment direction
                        llvm::errs() << "Warning: Cannot determine assignment direction for " << lhs.name
                                     << " == " << rhs.name << "\n";
                        blasted_region.insts.emplace_back("//", "(?)eq2assign: l=" + lhs.name + "." +
                                                                    std::to_string(lhs.topo_id) + " r=" + rhs.name +
                                                                    "." + std::to_string(rhs.topo_id));
                    }

                    // Check which side has not been defined
                    if (lhs_in_st || lhs.topo_id > rhs.topo_id) {
                        blasted_region.insts.emplace_back("=", ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                          ValueInfo{rhs.name, width, VProp::UNK, nullptr}, ValueInfo{});
                        return Z3VInfo(temp_name, Z3VType::Other);
                    }
                    if (rhs_in_st || lhs.topo_id < rhs.topo_id) {
                        blasted_region.insts.emplace_back("=", ValueInfo{rhs.name, width, VProp::UNK, nullptr},
                                                          ValueInfo{lhs.name, width, VProp::UNK, nullptr}, ValueInfo{});
                        return Z3VInfo(temp_name, Z3VType::Other);
                    }

                    return Z3VInfo(temp_name, Z3VType::Other);
                } else {
                    // A simple equivalence expression
                    auto lhs = traverseZ3Model(e.arg(0), state, depth + 1);
                    auto rhs = traverseZ3Model(e.arg(1), state, depth + 1);
                    blasted_region.insts.emplace_back("//", "== l=" + lhs.name + "." + std::to_string(lhs.topo_id) +
                                                                " r=" + rhs.name + "." + std::to_string(rhs.topo_id));
                    auto temp_name = Z3VInfo::getNewName();
                    Width width = 1;
                    blasted_region.insts.emplace_back("==", ValueInfo{temp_name, width, VProp::UNK, nullptr},
                                                      ValueInfo{lhs.name, width, VProp::UNK, nullptr},
                                                      ValueInfo{rhs.name, width, VProp::UNK, nullptr});
                    return Z3VInfo(temp_name, Z3VType::Other);
                }
            }

            // Handle an operation with multiple oprands
            blasted_region.insts.emplace_back("//",
                                              "OP '" + name + "' with " + std::to_string(e.num_args()) + " operands");
            for (unsigned i = 0; i < e.num_args(); i++) {
                auto child = traverseZ3Model(e.arg(i), state, depth + 1);
                blasted_region.insts.emplace_back("//", "No." + std::to_string(i) + " oprand: " + child.name +
                                                            " topo=" + std::to_string(child.topo_id));
                if (i == 0) {
                    blasted_region.insts.emplace_back("//", "MOVE:" + child.name);
                    prev = std::move(child);

                } else {
                    auto temp_name = Z3VInfo::getNewName();
                    llvm::errs() << "New name: " << temp_name << "\n";
                    const Width width = 1;

                    auto new_var = ValueInfo{temp_name, width, VProp::UNK,
                                             nullptr};  // !FIXME: we should not assign a new temp var
                    blasted_region.sym_tbl[temp_name] = new_var;
                    llvm::errs() << "New inst. depth:" << depth << " op" << name << " temp_name=" << temp_name << "\n";
                    blasted_region.insts.emplace_back(opname2operator(name, width), new_var,
                                                      ValueInfo{prev.name, width, VProp::UNK, nullptr},
                                                      ValueInfo{child.name, width, VProp::UNK, nullptr});
                    prev = Z3VInfo(temp_name, Z3VType::Other);
                    llvm::errs() << "prev updated: " << prev.name << " " << name << " " << child.name << "\n";
                }
            }
            llvm::errs() << "final prev: " << prev.name << "\n";
            return prev;
        } else if (name == "if") {
            // (ite k!7 (not (= k!5 k!4)) k!5)
            // then_expr: (not (= k!5 k!4))
            // else_expr: k!5
            // cond_expr: k!7
            // result = (then & !cond) | (else & cond)
            auto cond_z3 = traverseZ3Model(e.arg(0), state, depth + 1);
            auto then_z3 = traverseZ3Model(e.arg(1), state, depth + 1);
            auto else_z3 = traverseZ3Model(e.arg(2), state, depth + 1);
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

            blasted_region.insts.emplace_back("!", ncond_expr, ValueInfo{cond_z3.name, width, VProp::UNK, nullptr},
                                              ValueInfo{});
            blasted_region.insts.emplace_back("&", then_expr, ValueInfo{then_z3.name, width, VProp::UNK, nullptr},
                                              ValueInfo{cond_z3.name, 1, VProp::UNK, nullptr});
            blasted_region.insts.emplace_back("&", else_expr, ValueInfo{else_z3.name, width, VProp::UNK, nullptr},
                                              ValueInfo{ncond_expr.name, 1, VProp::UNK, nullptr});
            blasted_region.insts.emplace_back("|", result_expr, then_expr, else_expr);
            return Z3VInfo(result_name, Z3VType::Other);

        } else {
            llvm::errs() << "Unknown app: " << name << "\n";
        }
    } else if (e.is_quantifier()) {
        llvm::errs() << "quantifier: " << e.to_string() << "\n";
        blasted_region.insts.emplace_back("//", "!unknown quantifier " + e.to_string());

    } else {
        llvm::errs() << "UNKNOWN node: " << e.to_string() << "\n";
        blasted_region.insts.emplace_back("//", "!unknown node " + e.to_string());
    }
    return Z3VInfo{};
}
void Z3BitBlastPass::solve_and_extract(const z3::goal &goal) {
    // Set bit-blasting tactic
    z3::tactic simplify{z3ctx, "simplify"};

    z3::tactic t{z3ctx, "bit-blast"};
    z3::params p{z3ctx};
    p.set("blast_full", true);
    z3::tactic bit_blast = with(t, p);

    z3::tactic simplify_again{z3ctx, "simplify"};

    auto optimize_tactic = simplify & bit_blast & simplify_again;

    // Apply the tactic to blast
    z3::apply_result result = optimize_tactic(goal);

    llvm::errs() << "------------------\n";

    llvm::errs() << "- Z3 tree:\n";
    for (unsigned i = 0; i < result.size(); ++i) {
        llvm::errs() << result[i].as_expr().to_string() << "\n";
        traverseZ3Model(result[i].as_expr(), 0, 0);
    }

    // Dump varbit2id
    llvm::errs() << "- varbit2id:\n";
    for (const auto &v2i : varbit2id) {
        llvm::errs() << v2i.first << " -> " << v2i.second << "\n";
    }

    llvm::errs() << "------------------\n";
}

Region Z3BitBlastPass::get() {
    // Assemble output vars from bits at the end of the function body
    for (const auto &[vinfo, bits] : var2bits) {
        if (vinfo.prop == VProp::OUTPUT) {
            blasted_region.insts.emplace_back("/clear/", vinfo, ValueInfo{"0", 1, VProp::CST, nullptr}, ValueInfo{});
            for (unsigned i = 0; i < bits.size(); ++i) {
                if (bits[i].has_value()) {
                    auto var_bit_name = vinfo.name + "#" + std::to_string(i);
                    blasted_region.insts.emplace_back("/z3=>var/", vinfo,
                                                      ValueInfo{var_bit_name, 1, VProp::CST, nullptr},
                                                      ValueInfo{std::to_string(i), 1, VProp::CST, nullptr});
                }
            }
        }
    }

    llvm::errs() << "blasted region:\n";
    blasted_region.dump();

    return blasted_region;
}