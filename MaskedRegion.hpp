#pragma once

#include "CombinedRegion.hpp"
#include "DataStructures.hpp"
#include "RegionDivider.hpp"
#include <cassert>
#include <vector>

class MaskedRegion {};

class TrivialMaskedRegion : MaskedRegion {
public:
  TrivialMaskedRegion(Region &originalRegion) {
    assert(originalRegion.count() == 1);
    region.st = originalRegion.st;
    region.outputs2xored.insert(originalRegion.outputs2xored.begin(),
                                originalRegion.outputs2xored.end());
    // No instruments initially

    for (const auto &inst : originalRegion.instructions) {
      auto maskedInsts = replaceInstruction(inst); // 1->n
      for (const auto &newInst : maskedInsts) {
        region.append(newInst);
      }
    }
  }
  TrivialMaskedRegion(std::vector<Instruction> insts) {
    // TODO!
  }

public:
  Region region;

private:
  std::vector<Instruction> replaceInstruction(const Instruction &inst) {
    std::vector<Instruction> newInsts;

    const auto &A = inst.lhs;
    const auto &B = inst.rhs;
    const auto &op = inst.op;
    const auto &assignTo = inst.assignTo;

    // TODO: avoid naming conflicts

    // TRICK: A|B == ~( (~A) & (~B) )
    // need optimization!
    if (op == "|") {
      ValueInfo nA(assignTo.name + "nA", VType::Bool, VProp::UNK, nullptr);
      ValueInfo nB(assignTo.name + "nB", VType::Bool, VProp::UNK, nullptr);
      ValueInfo andNN(assignTo.name + "and", VType::Bool, VProp::MASKED,
                      nullptr);
      region.st[nA.name] = nA;
      region.st[nB.name] = nB;
      region.st[andNN.name] = andNN;

      newInsts.emplace_back("~", nA, A, ValueInfo());
      newInsts.emplace_back("~", nB, B, ValueInfo());
      newInsts.emplace_back("&", andNN, nA, nB);
      newInsts.emplace_back("~", assignTo, andNN, ValueInfo());
      Region realReplaced;
      realReplaced.instructions = newInsts;
      TrivialRegionDivider realDivided(realReplaced);
      CombinedRegion res;
      while (!realDivided.done()) {
        Region subRegion = realDivided.next();
        TrivialMaskedRegion maskedRegion(subRegion);
        res.add(std::move(maskedRegion.region));
      }
      region.st.insert(res.curRegion.st.begin(), res.curRegion.st.end());
      const auto &nested = res.curRegion.outputs2xored[assignTo.name];
      region.outputs2xored[assignTo.name] = nested;

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
      ValueInfo mA(assignTo.name + "mA", VType::Bool, VProp::MASKED, nullptr);
      ValueInfo mB(assignTo.name + "mB", VType::Bool, VProp::MASKED, nullptr);
      ValueInfo mR(assignTo.name + "mR", VType::Bool, VProp::MASKED, nullptr);
      ValueInfo mT(assignTo.name + "mT", VType::Bool, VProp::MASKED, nullptr);

      region.st[r1.name] = r1;
      region.st[r2.name] = r2;
      region.st[mA.name] = mA;
      region.st[mB.name] = mB;
      region.st[mR.name] = mR;
      region.st[mT.name] = mT;

      newInsts.emplace_back("^", mA, A, r1);
      newInsts.emplace_back("^", mB, B, r2);
      newInsts.emplace_back("^", mT, mA, mB);
      newInsts.emplace_back("^", mR, r1, r2);
      newInsts.emplace_back("^", assignTo, mR, mT);

      region.outputs2xored[assignTo.name] = mT;
      return newInsts;
    }

    else if (op == "~" || op == "!") {
      // NOT: T=~A ->
      // mA=A^r1;
      // mT=~mA;
      // T=mT^r1

      ValueInfo r1 = ValueInfo::getNewRand();
      ValueInfo mA(assignTo.name + "mA", VType::Bool, VProp::MASKED, nullptr);
      ValueInfo mT(assignTo.name + "mT", VType::Bool, VProp::MASKED, nullptr);

      region.st[r1.name] = r1;
      region.st[mA.name] = mA;
      region.st[mT.name] = mT;

      newInsts.emplace_back("^", mA, A, r1);
      newInsts.emplace_back("~", mT, mA, ValueInfo());
      newInsts.emplace_back("^", assignTo, mT, r1);

      region.outputs2xored[assignTo.name] = r1;
      return newInsts;
    }

    else if (op == "&") {
      // AND: T = A & B ->
      // mA = A ^ r1;
      // mB = B ^ r2;
      // negmB = ~mB;
      // mAr2 = mA & r2;
      // negr3 = ~r3;
      // tmp1 = negmB & r3;
      // tmp2 = mB & mA;
      // tmp3 = ~mAr2;
      // tmp4 = negr3 | r2;
      // tmp5 = tmp1 | tmp2;
      // tmp6 = tmp3 ^ tmp4;
      // T = tmp5 ^ tmp6;

      ValueInfo r1 = ValueInfo::getNewRand();
      ValueInfo r2 = ValueInfo::getNewRand();
      ValueInfo r3 = ValueInfo::getNewRand();
      ValueInfo mA(A.name + "mA", VType::Bool, VProp::MASKED, nullptr);
      ValueInfo mB(B.name + "mB", VType::Bool, VProp::MASKED, nullptr);
      ValueInfo negmB(mB.name + "neg", VType::Bool, VProp::UNK, nullptr);
      ValueInfo mAr2(mA.name + "r2", VType::Bool, VProp::UNK, nullptr);
      ValueInfo negr3(r3.name + "neg", r3.type, VProp::UNK, nullptr);
      ValueInfo tmp1(assignTo.name + "tmp1", VType::Bool, VProp::UNK, nullptr);
      ValueInfo tmp2(assignTo.name + "tmp2", VType::Bool, VProp::UNK, nullptr);
      ValueInfo tmp3(assignTo.name + "tmp3", VType::Bool, VProp::UNK, nullptr);
      ValueInfo tmp4(assignTo.name + "tmp4", VType::Bool, VProp::UNK, nullptr);
      ValueInfo tmp5(assignTo.name + "tmp5", VType::Bool, VProp::UNK, nullptr);
      ValueInfo tmp6(assignTo.name + "tmp6", VType::Bool, VProp::UNK, nullptr);

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
      newInsts.emplace_back("^", mA, A, r1);
      newInsts.emplace_back("^", mB, B, r2);
      newInsts.emplace_back("~", negmB, mB, ValueInfo());
      newInsts.emplace_back("&", mAr2, mA, r2);
      newInsts.emplace_back("~", negr3, r3, ValueInfo());
      newInsts.emplace_back("&", tmp1, negmB, r3);
      newInsts.emplace_back("&", tmp2, mB, mA);
      newInsts.emplace_back("~", tmp3, mAr2, ValueInfo());
      newInsts.emplace_back("|", tmp4, negr3, r2);
      newInsts.emplace_back("|", tmp5, tmp1, tmp2);
      newInsts.emplace_back("^", tmp6, tmp3, tmp4);
      newInsts.emplace_back("^", assignTo, tmp5, tmp6);

      region.outputs2xored[assignTo.name] = tmp6;
      return newInsts;
    }

    // default
    newInsts.push_back(inst);
    return newInsts;
  }
};
