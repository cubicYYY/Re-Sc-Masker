#include "BitBlast.hpp"
#include "DataStructures.hpp"
#include <cassert>
#include <llvm-16/llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <utility>
#include <z3++.h>

// FIXME: For multiple regions, we may need to re-assemble bits of a temporary
// variable before passing it to the next region. Of course that will cause
// redundant assembling, but it is a good start.
BitBlastPass::BitBlastPass(SymbolTable st, ValueInfo ret) {
  region.st = st;
  // Iterate over all vars to register bits
  // first: name; second: ValueInfo;
  for (const auto &var : st) {

    const std::string &var_name = var.first;
    z3::expr z3var = z3ctx.bv_const(var_name.c_str(), var.second.width);
    var_expr.emplace(var.second, z3var);

    // We have #width bits for each var
    var_bits[var.second].resize(var.second.width);

    for (auto i = 0; i < var.second.width; i++) {
      auto mask = z3ctx.bv_val((uint64_t(1) << i), var.second.width);
      std::string var_bit_name = var_name + "#" + std::to_string(i);
      z3::expr bit_i = z3ctx.bool_const(var_bit_name.c_str());

      z3goal.add(bit_i == ((z3var & mask) == mask));

      var_bits[var.second][i] = std::optional<z3::expr>(bit_i);
    }
  }

  // Register for the return value
  this->ret = ret;
  const std::string &var_name = ret.name;
  z3::expr z3var = z3ctx.bv_const(var_name.c_str(), ret.width);
  var_bits[ret].resize(ret.width);

  for (auto i = 0; i < ret.width; i++) {
    auto mask = z3ctx.bv_val((uint64_t(1) << i), ret.width);

    std::string var_bit_name = var_name + "#" + std::to_string(i);
    z3::expr bit_i = z3ctx.bool_const(var_bit_name.c_str());
    z3goal.add(bit_i == ((z3var & mask) == mask));
    var_bits[ret][i] = std::optional<z3::expr>(bit_i);
  }
}

void BitBlastPass::add(Region &&r) {
  // TODO: Add the constraints from instructions
  for (const auto &inst : r.instructions) {
    inst.dump();
    if (inst.op == "=") {
      // Assign operation: a = b
      auto target_expr = var_expr[inst.assign_to].value();
      auto left_expr = var_expr[inst.lhs].value();
      z3goal.add(target_expr == left_expr);
    } else if (inst.op == "^") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (left_expr ^ right_expr));
    } else if (inst.op == "|") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (left_expr | right_expr));
    } else if (inst.op == "&") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (left_expr & right_expr));
    } else if (inst.op == "~") {
      auto left_expr = var_expr[inst.lhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (~left_expr));
    } else if (inst.op == "*") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (left_expr * right_expr));
    } else if (inst.op == "+") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (left_expr + right_expr));
    } else if (inst.op == "-") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assign_to].value();
      z3goal.add(target_expr == (left_expr - right_expr));
    } else if (inst.op == "!") {
      llvm::errs() << "Not implemented: " << inst.op << "\n";
    } else {
      llvm::errs() << "Not implemented: " << inst.op << "\n";
    }
  }
}

Z3VInfo BitBlastPass::traverseZ3Model(const z3::expr &e, int depth) {

  for (auto i = 0; i < depth * 4; i++) {
    llvm::errs() << (i % 4 ? "-" : "|");
  }

  if (e.is_var()) {
    llvm::errs() << "var: " << e.to_string() << "\n";
    return Z3VInfo(e.to_string(), Z3VType::Other); // FIXME: May be input/output
  } else if (e.is_const()) {
    llvm::errs() << "const: " << e.to_string() << "\n";
    return Z3VInfo(e.to_string(), Z3VType::Other); // FIXME: May be input/output
  } else if (e.is_not()) {
    llvm::errs() << "~\n";
    // auto width = e.arg(0).get_sort().bv_size();
    Width width = 1;

    auto oprand = traverseZ3Model(e.arg(0), depth + 1);

    if (depth ==
        1) { // Rewrite `a = (v==expr / expr == v)` to `tmp=expr; v=!tmp;`
      auto lhs = region.instructions.back().lhs;
      auto rhs = region.instructions.back().rhs;
      auto new_var_name = Z3VInfo::getNewName();
      auto new_var = ValueInfo{new_var_name, width, VProp::UNK, nullptr};
      region.st[new_var_name] = new_var;
      // FIXME: We need to know which side should be the expr, and which side
      // should be the v
      auto origin_var = region.instructions.back().rhs;
      region.instructions.back() = Instruction{
          "=", new_var, region.instructions.back().lhs, ValueInfo{}};
      region.instructions.emplace_back(
          Instruction{"!", origin_var, new_var, ValueInfo{}});
      return Z3VInfo(new_var_name, Z3VType::Other);
    }

    std::string temp_name = Z3VInfo::getNewName();
    auto new_var = ValueInfo{temp_name, width, VProp::UNK, nullptr};
    region.st[temp_name] = new_var;
    // Use `!` instead of `~` for boolean variables!
    llvm::errs() << "New inst. depth:" << depth << " op~!\n";
    region.instructions.emplace_back(Instruction{
        width == 1 ? "!" : "~", new_var,
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
        (e.arg(0).is_const() ||
         e.arg(1).is_const())) { // This is an potential aliasing!
      if (e.arg(0).is_const() &&
          e.arg(0).decl().name().kind() == Z3_STRING_SYMBOL) { // rhs
        auto alias_id = e.arg(1).decl().name().to_int();
        auto alias_name = "k!" + std::to_string(alias_id);
        auto origin_name = e.arg(0).decl().name().str();
        auto origin_var = origin_name.substr(0, origin_name.find('#'));
        auto origin_prop = region.st[origin_var].prop;

        alias_of[alias_id] = origin_name;
        z3alias[origin_name] = alias_id;
        llvm::errs() << "alias_of[" << alias_id << "] = " << origin_name
                     << "\n";
        llvm::errs() << alias_id << " (alias)\n";

        // The property of the alias is the same as the origin variable
        region.st[alias_name] = ValueInfo{alias_name, 1, origin_prop, nullptr};
        return Z3VInfo{"!ALIAS", Z3VType::Other};
      }
      if (e.arg(1).is_const() &&
          e.arg(1).decl().name().kind() == Z3_STRING_SYMBOL) { // lhs
        auto alias_id = e.arg(0).decl().name().to_int();
        auto alias_name = "k!" + std::to_string(alias_id);
        auto origin_name = e.arg(1).decl().name().str();
        auto origin_var = origin_name.substr(0, origin_name.find('#'));
        auto origin_prop = region.st[origin_var].prop;

        alias_of[alias_id] = origin_name;
        z3alias[origin_name] = alias_id;
        llvm::errs() << "alias_of[" << alias_id << "] = " << origin_name
                     << "\n";
        llvm::errs() << alias_id << " (alias)\n";
        region.st[alias_name] = ValueInfo{alias_name, 1, origin_prop, nullptr};
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
        bool lhs_in_st = region.st.find(lhs.name) != region.st.end();
        bool rhs_in_st = region.st.find(rhs.name) != region.st.end();
        auto lhs_prop = region.st[lhs.name].prop;
        auto rhs_prop = region.st[rhs.name].prop;

        if (lhs_prop == VProp::RND || lhs_prop == VProp::SECRET ||
            lhs_prop == VProp::PUB) {
          // If LHS is input type, RHS should be assigned LHS value
          region.instructions.emplace_back(Instruction{
              "=", ValueInfo{rhs.name, width, VProp::UNK, nullptr},
              ValueInfo{lhs.name, width, VProp::UNK, nullptr}, ValueInfo{}});
          return Z3VInfo(temp_name, Z3VType::Other);

        } else if (rhs_prop == VProp::RND || rhs_prop == VProp::SECRET ||
                   rhs_prop == VProp::PUB) {
          // If LHS not defined but RHS exists, assign RHS to LHS
          region.instructions.emplace_back(Instruction{
              "=", ValueInfo{lhs.name, width, VProp::UNK, nullptr},
              ValueInfo{rhs.name, width, VProp::UNK, nullptr}, ValueInfo{}});
          return Z3VInfo(temp_name, Z3VType::Other);
        }

        // If we reach here, we can't determine the correct assignment direction
        llvm::errs() << "Warning: Cannot determine assignment direction for "
                     << lhs.name << " == " << rhs.name << "\n";

        // Check which side has not been defined
        if (lhs_in_st) {
          region.instructions.emplace_back(Instruction{
              "=", ValueInfo{lhs.name, width, VProp::UNK, nullptr},
              ValueInfo{rhs.name, width, VProp::UNK, nullptr}, ValueInfo{}});
        } else {
          region.instructions.emplace_back(Instruction{
              "=", ValueInfo{rhs.name, width, VProp::UNK, nullptr},
              ValueInfo{lhs.name, width, VProp::UNK, nullptr}, ValueInfo{}});
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
          region.st[temp_name] = new_var;
          llvm::errs() << "New inst. depth:" << depth << " op" << opname
                       << "\n";
          region.instructions.emplace_back(Instruction{
              opname, new_var, ValueInfo{prev.name, width, VProp::UNK, nullptr},
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
      auto ncond_expr_name = Z3VInfo::getNewName(); // i.e. !cond
      auto result_name = Z3VInfo::getNewName()+"_ite";     // i.e. !cond
      auto then_expr = ValueInfo{then_expr_name, width, VProp::UNK, nullptr};
      region.st[then_expr_name] = then_expr;
      auto else_expr = ValueInfo{else_expr_name, width, VProp::UNK, nullptr};
      region.st[else_expr_name] = else_expr;
      auto ncond_expr = ValueInfo{ncond_expr_name, width, VProp::UNK, nullptr};
      region.st[ncond_expr_name] = ncond_expr;
      auto result_expr = ValueInfo{result_name, width, VProp::UNK, nullptr};
      region.st[result_name] = result_expr;

      region.instructions.emplace_back(Instruction{
          "!", ncond_expr, ValueInfo{cond_z3.name, width, VProp::UNK, nullptr},
          ValueInfo{}});
      region.instructions.emplace_back(Instruction{
          "&", then_expr, ValueInfo{then_z3.name, width, VProp::UNK, nullptr},
          ValueInfo{cond_z3.name, 1, VProp::UNK, nullptr}});
      region.instructions.emplace_back(Instruction{
          "&", else_expr, ValueInfo{else_z3.name, width, VProp::UNK, nullptr},
          ValueInfo{ncond_expr.name, 1, VProp::UNK, nullptr}});
      region.instructions.emplace_back(
          Instruction{"|", result_expr, then_expr, else_expr});
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

Region BitBlastPass::get() {
  // Simplify and bit-blast
  z3::tactic simplify(z3ctx, "simplify");
  z3::tactic bit_blast(z3ctx, "bit-blast");
  z3::tactic simplify_again(z3ctx, "simplify");
  z3::tactic combined = simplify & bit_blast & simplify_again;

  // Apply simplify->bit-blast->simplify methods
  z3::apply_result result = combined(z3goal);

  llvm::errs() << "Simplified and bit-blasted constraints:\n";
  for (unsigned i = 0; i < result.size(); ++i) {
    llvm::errs() << result[i].as_expr().to_string() << "\n";
    traverseZ3Model(result[i].as_expr());
  }

  // Dump z3alias
  llvm::errs() << "z3alias:\n";
  for (const auto &alias : z3alias) {
    llvm::errs() << alias.first << " -> " << alias.second << "\n";
  }

  // Extract input variable bits before function body
  std::vector<Instruction> temp_instructions;
  llvm::errs() << "inserting input bits:\n";
  for (const auto &var_bit : var_bits) {
    if (var_bit.first.prop == VProp::PUB ||
        var_bit.first.prop == VProp::SECRET) { // Only for input variables
      llvm::errs() << "inserting input bits for " << var_bit.first.name << "\n";
      for (unsigned i = 0; i < var_bit.second.size(); ++i) {
        auto var_bit_name = var_bit.first.name + "#" + std::to_string(i);
        assert(var_bit.second[i].has_value() && "empty bit");
        temp_instructions.emplace_back(
            Instruction{"/z3=/",
                        ValueInfo{"k!" + std::to_string(z3alias[var_bit_name]),
                                  1, VProp::CST, nullptr},
                        var_bit.first,
                        ValueInfo{std::to_string(i), 1, VProp::CST, nullptr}});
      }
    }
  }

  // Insert all temp instructions at the beginning
  region.instructions.insert(region.instructions.begin(),
                             temp_instructions.begin(),
                             temp_instructions.end());

  // Assemble output vars from bits before return
  for (const auto &var_bit : var_bits) {
    if (var_bit.first.prop == VProp::OUTPUT) {
      region.instructions.emplace_back(
          Instruction{"=", var_bit.first,
                      ValueInfo{"0", 1, VProp::CST, nullptr}, ValueInfo{}});
      for (unsigned i = 0; i < var_bit.second.size(); ++i) {
        if (var_bit.second[i].has_value()) {
          auto var_bit_name = var_bit.first.name + "#" + std::to_string(i);
          region.instructions.emplace_back(Instruction{
              "/z3|=/", var_bit.first,
              ValueInfo{"k!" + std::to_string(z3alias[var_bit_name]), 1,
                        VProp::CST, nullptr},
              ValueInfo{std::to_string(i), 1, VProp::CST, nullptr}});
        }
      }
    }
  }
  llvm::errs() << "blasted region:\n";
  region.dump();
  return region;
}