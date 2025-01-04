#include "MaskedRegion.hpp"
#include "DefUseRegion.hpp"
#include "FinalRegion.hpp"
#include "RegionDivider.hpp"
#include <llvm-16/llvm/Support/raw_ostream.h>

void TrivialMaskedRegion::dump() const {
  llvm::errs() << "----!Dumping: tmr\n";
  // region.dump();
  for (auto out : outputs) {
    llvm::errs() << out.name << ";";
  }
  llvm::errs() << "----!dumped\n";
}
TrivialMaskedRegion::TrivialMaskedRegion(Region &originalRegion) {
  assert(originalRegion.count() == 1);
  region.st = originalRegion.st;
  // No instruments initially
  for (const auto &inst : originalRegion.instructions) {
    auto maskedInsts = replaceInstruction(inst); // 1->n
    region.instructions.insert(region.instructions.end(), maskedInsts.begin(),
                               maskedInsts.end());
    outputs.insert(inst.assignTo);
  }
}

void TrivialMaskedRegion::issueNewInst(std::vector<Instruction> &newInsts,
                                       std::string_view op, const ValueInfo &t,
                                       const ValueInfo &a, const ValueInfo &b) {
  newInsts.emplace_back(op, t, a, b);
}
std::vector<Instruction>
TrivialMaskedRegion::replaceInstruction(const Instruction &inst) {
  std::vector<Instruction> new_insts;

  const auto &A = inst.lhs;
  const auto &B = inst.rhs;
  const auto &op = inst.op;
  const auto &assignTo = inst.assignTo;

  // TODO: avoid naming conflicts

  // TRICK: A|B == !( (!A) & (!B) )
  // need optimization!
  if (op == "|" || op == "||") {
    ValueInfo nA(assignTo.name + "ornA", 1, VProp::UNK, nullptr);
    ValueInfo nB(assignTo.name + "ornB", 1, VProp::UNK, nullptr);
    ValueInfo andNN(assignTo.name + "orand", 1, VProp::MASKED,
                    nullptr);
    region.st[nA.name] = nA;
    region.st[nB.name] = nB;
    region.st[andNN.name] = andNN;

    // Do not call issueNewInst since we dont want side effects in the future
    new_insts.emplace_back("!", nA, A, ValueInfo());
    new_insts.emplace_back("!", nB, B, ValueInfo());
    new_insts.emplace_back("&&", andNN, nA, nB);
    new_insts.emplace_back("!", assignTo, andNN, ValueInfo());
    Region realReplaced;
    realReplaced.instructions = new_insts;

    TrivialRegionDivider realDivided(realReplaced);

    DefUseCombinedRegion defuse;
    while (!realDivided.done()) {
      Region subRegion = realDivided.next();
      defuse.add(TrivialMaskedRegion(subRegion));
    }

    FinalRegion res{std::move(defuse)};
    region.st.insert(res.curRegion.st.begin(), res.curRegion.st.end());

    return res.curRegion.instructions;
  }

  else if (op == "^") {
    // XOR: T=A^B ->
    // mA=A^r1;
    // mB=B^r2;
    // mT=mA^mB;
    // mR=r1^r2;
    // T=mT^mR;

    ValueInfo r1 = ValueInfo::getNewRand();
    ValueInfo r2 = ValueInfo::getNewRand();
    ValueInfo mA(assignTo.name + "xormA", 1, VProp::MASKED, nullptr);
    ValueInfo mB(assignTo.name + "xormB", 1, VProp::MASKED, nullptr);
    ValueInfo mR(assignTo.name + "xormR", 1, VProp::MASKED, nullptr);
    ValueInfo mT(assignTo.name + "xormT", 1, VProp::MASKED, nullptr);

    region.st[r1.name] = r1;
    region.st[r2.name] = r2;
    region.st[mA.name] = mA;
    region.st[mB.name] = mB;
    region.st[mR.name] = mR;
    region.st[mT.name] = mT;

    issueNewInst(new_insts, "^", mA, A, r1);
    issueNewInst(new_insts, "^", mB, B, r2);
    issueNewInst(new_insts, "^", mT, mA, mB);
    issueNewInst(new_insts, "^", mR, r1, r2);
    issueNewInst(new_insts, "^", assignTo, mR, mT);

    return new_insts;
  }

  else if (op == "!" || op == "~") {
    // NOT: T=!A ->
    // mA=A^r1;
    // mT=!mA;
    // T=mT^r1

    ValueInfo r1 = ValueInfo::getNewRand();
    ValueInfo mA(assignTo.name + "notmA", 1, VProp::MASKED, nullptr);
    ValueInfo mT(assignTo.name + "notmT", 1, VProp::MASKED, nullptr);

    region.st[r1.name] = r1;
    region.st[mA.name] = mA;
    region.st[mT.name] = mT;

    issueNewInst(new_insts, "^", mA, A, r1);
    issueNewInst(new_insts, "!", mT, mA, ValueInfo());
    issueNewInst(new_insts, "^", assignTo, mT, r1);

    return new_insts;
  }

  else if (op == "&" || op == "&&") {
    // AND: T = A & B ->
    // mA = A ^ r1;
    // mB = B ^ r2;
    // negmB = !mB;
    // mAr2 = mA & r2;
    // negr3 = !r3;
    // tmp1 = negmB & r3;
    // tmp2 = mB & mA;
    // tmp3 = !mAr2;
    // tmp4 = negr3 | r2;
    // tmp5 = tmp1 | tmp2;
    // tmp6 = tmp3 ^ tmp4;
    // T = tmp5 ^ tmp6;

    ValueInfo r1 = ValueInfo::getNewRand();
    ValueInfo r2 = ValueInfo::getNewRand();
    ValueInfo r3 = ValueInfo::getNewRand();
    ValueInfo mA(assignTo.name + "andmA", 1, VProp::MASKED, nullptr);
    ValueInfo mB(assignTo.name + "andmB", 1, VProp::MASKED, nullptr);
    ValueInfo negmB(assignTo.name + "andneg1", 1, VProp::UNK,
                    nullptr);
    ValueInfo mAr2(assignTo.name + "andr2", 1, VProp::UNK, nullptr);
    ValueInfo negr3(assignTo.name + "andneg2", r3.width, VProp::UNK, nullptr);
    ValueInfo tmp1(assignTo.name + "andtmp1", 1, VProp::UNK, nullptr);
    ValueInfo tmp2(assignTo.name + "andtmp2", 1, VProp::UNK, nullptr);
    ValueInfo tmp3(assignTo.name + "andtmp3", 1, VProp::UNK, nullptr);
    ValueInfo tmp4(assignTo.name + "andtmp4", 1, VProp::UNK, nullptr);
    ValueInfo tmp5(assignTo.name + "andtmp5", 1, VProp::UNK, nullptr);
    ValueInfo tmp6(assignTo.name + "andtmp6", 1, VProp::UNK, nullptr);

    region.st[r1.name] = r1;
    region.st[r2.name] = r2;
    region.st[r3.name] = r3;
    region.st[mA.name] = mA;
    region.st[mB.name] = mB;
    region.st[negmB.name] = negmB;
    region.st[mAr2.name] = mAr2;
    region.st[negr3.name] = negr3;
    region.st[tmp1.name] = tmp1;
    region.st[tmp2.name] = tmp2;
    region.st[tmp3.name] = tmp3;
    region.st[tmp4.name] = tmp4;
    region.st[tmp5.name] = tmp5;
    region.st[tmp6.name] = tmp6;

    // Mask A and B with random values
    issueNewInst(new_insts, "^", mA, A, r1);
    issueNewInst(new_insts, "^", mB, B, r2);
    issueNewInst(new_insts, "!", negmB, mB, ValueInfo());
    issueNewInst(new_insts, "&&", mAr2, mA, r2);
    issueNewInst(new_insts, "!", negr3, r3, ValueInfo());
    issueNewInst(new_insts, "&&", tmp1, negmB, r3);
    issueNewInst(new_insts, "&&", tmp2, mB, mA);
    issueNewInst(new_insts, "!", tmp3, mAr2, ValueInfo());
    issueNewInst(new_insts, "||", tmp4, negr3, r2);
    issueNewInst(new_insts, "||", tmp5, tmp1, tmp2);
    issueNewInst(new_insts, "^", tmp6, tmp3, tmp4);
    issueNewInst(new_insts, "^", assignTo, tmp5, tmp6);

    return new_insts;
  }

  // default
  new_insts.push_back(inst);
  return new_insts;
}