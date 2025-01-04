#include "BitBlast.hpp"
#include "DataStructures.hpp"
#include <llvm-16/llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <utility>
#include <z3++.h>

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
    if (inst.op == "=") {
      // Assign operation: a = b
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();

      z3goal.add(left_expr == right_expr);
    } else if (inst.op == "^") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assignTo].value();
      z3goal.add(target_expr == (left_expr ^ right_expr));
    } else if (inst.op == "&") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assignTo].value();
      z3goal.add(target_expr == (left_expr & right_expr));
    } else if (inst.op == "~") {
      auto left_expr = var_expr[inst.lhs].value();
      auto target_expr = var_expr[inst.assignTo].value();
      z3goal.add(target_expr == (~left_expr));
    } else if (inst.op == "*") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assignTo].value();
      z3goal.add(target_expr == (left_expr * right_expr));
    } else if (inst.op == "+") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assignTo].value();
      z3goal.add(target_expr == (left_expr + right_expr));
    } else if (inst.op == "-") {
      auto left_expr = var_expr[inst.lhs].value();
      auto right_expr = var_expr[inst.rhs].value();
      auto target_expr = var_expr[inst.assignTo].value();
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

    // FIXME: If this operand is an assignment, reverse the right side
    // llvm::errs()<<"New name not"<<"\n";
    std::string temp_name = Z3VInfo::getNewName();
    auto new_var = ValueInfo{temp_name, width, VProp::UNK, nullptr};
    region.st[temp_name] = new_var;
    // Use `!` instead of `~` for boolean variables!
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
        alias_of[alias_id] = e.arg(0).decl().name().str();
        llvm::errs() << alias_id << " (alias)\n";
        region.st[alias_name] = ValueInfo{alias_name, 1, VProp::UNK, nullptr};
        return Z3VInfo{"!ALIAS", Z3VType::Other};
      }
      if (e.arg(1).is_const() &&
          e.arg(1).decl().name().kind() == Z3_STRING_SYMBOL) { // lhs
        auto alias_id = e.arg(0).decl().name().to_int();
        auto alias_name = "k!" + std::to_string(alias_id);
        alias_of[alias_id] = e.arg(1).decl().name().str();
        llvm::errs() << alias_id << " (alias)\n";
        region.st[alias_name] = ValueInfo{alias_name, 1, VProp::UNK, nullptr};
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
      for (unsigned i = 0; i < e.num_args(); i++) {
        auto child = traverseZ3Model(e.arg(i), depth + 1);
        if (i == 0) {
          prev = std::move(child);
        } else {
          // AND at the top level

          llvm::errs() << "New name: " << opname << "\n";
          auto temp_name = Z3VInfo::getNewName();
          //   auto width = e.arg(i).get_sort().bv_size();
          Width width = 1;

          auto new_var = ValueInfo{temp_name, width, VProp::UNK, nullptr};
          region.st[temp_name] = new_var;
          region.instructions.emplace_back(Instruction{
              opname, new_var, ValueInfo{prev.name, width, VProp::UNK, nullptr},
              ValueInfo{child.name, width, VProp::UNK, nullptr}});
          prev = Z3VInfo(temp_name, Z3VType::Other);
          llvm::errs() << "prev: " << prev.name << "+=" << child.name << "\n";
        }
      }
    } else {
      llvm::errs() << "Unknown app: " << name << "\n";
    }
    llvm::errs() << "prev: " << prev.name << "\n";
    return prev;
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
  region.dump();
  return region;
}