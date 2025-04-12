#include "Re-Sc-Masker/RegionMasker.hpp"

#include <llvm-16/llvm/Support/raw_ostream.h>

#include "Re-Sc-Masker/RegionCollector.hpp"
#include "Re-Sc-Masker/RegionConcatenater.hpp"
#include "Re-Sc-Masker/RegionDivider.hpp"

void TrivialRegionMasker::dump() const {
    llvm::errs() << "\n(trivial masked)\n";
    region.dump();
    for (auto out : outputs) {
        llvm::errs() << out.name << ";";
    }
    llvm::errs() << "\n----\n";
}
TrivialRegionMasker::TrivialRegionMasker(Region &originalRegion) {
    assert(originalRegion.count() == 2);  // original inst + region divider comment
    region.st = originalRegion.st;
    // No instruments initially
    for (const auto &inst : originalRegion.instructions) {
        if (inst.op == "//") {
            region.instructions.push_back(inst);
            continue;
        }
        auto maskedInsts = replaceInstruction(inst);  // 1->n
        region.instructions.insert(region.instructions.end(), maskedInsts.begin(), maskedInsts.end());
        outputs.insert(inst.assign_to);
    }
}

void TrivialRegionMasker::issueNewInst(std::vector<Instruction> &newInsts, std::string_view op, const ValueInfo &t,
                                       const ValueInfo &a, const ValueInfo &b) {
    newInsts.emplace_back(op, t, a, b);
}
std::vector<Instruction> TrivialRegionMasker::replaceInstruction(const Instruction &inst) {
    std::vector<Instruction> new_insts;

    const auto &A = inst.lhs;
    const auto &B = inst.rhs;
    const auto &op = inst.op;
    const auto &assign_to = inst.assign_to;

    // TODO: avoid naming conflicts
    if (inst.op == "//") {
        new_insts.push_back(inst);
        return new_insts;
    }
    // TRICK: A|B == !( (!A) & (!B) )
    // need optimization!
    if (op == "|" || op == "||") {
        ValueInfo nA(assign_to.name + "ornA", 1, VProp::UNK, nullptr);
        ValueInfo nB(assign_to.name + "ornB", 1, VProp::UNK, nullptr);
        ValueInfo andNN(assign_to.name + "orand", 1, VProp::MASKED, nullptr);
        region.st[nA.name] = nA;
        region.st[nB.name] = nB;
        region.st[andNN.name] = andNN;

        // Do not call issueNewInst since we dont want side effects in the future
        new_insts.emplace_back("!", nA, A, ValueInfo());
        new_insts.emplace_back("!", nB, B, ValueInfo());
        new_insts.emplace_back("&&", andNN, nA, nB);
        new_insts.emplace_back("!", assign_to, andNN, ValueInfo());
        Region realReplaced;
        realReplaced.instructions = new_insts;

        TrivialRegionDivider realDivided(realReplaced);

        RegionCollector<TrivialRegionMasker> defuse;
        while (!realDivided.done()) {
            Region subRegion = realDivided.next();
            defuse.add(TrivialRegionMasker(subRegion));
        }

        RegionConcatenater res{std::move(defuse)};
        region.st.insert(res.curRegion.st.begin(), res.curRegion.st.end());

        return res.curRegion.instructions;
    }

    else if (op == "==") {
        // EQ: T=(A==B)=!(A^B) ->
        // mA=A^r1;
        // mB=B^r2;
        // mT=mA^mB;
        // mR=r1^r2;
        // T_=mT^r3;
        // mC = T_^mR;
        // Tr3 = ~mC;
        // T = Tr3^r3;

        ValueInfo r1 = ValueInfo::getNewRand();
        ValueInfo r2 = ValueInfo::getNewRand();
        ValueInfo mA(assign_to.name + "xormA", 1, VProp::MASKED, nullptr);
        ValueInfo mB(assign_to.name + "xormB", 1, VProp::MASKED, nullptr);
        ValueInfo mR(assign_to.name + "xormR", 1, VProp::MASKED, nullptr);
        ValueInfo mT(assign_to.name + "xormT", 1, VProp::MASKED, nullptr);
        ValueInfo T_(assign_to.name + "xormT_", 1, VProp::MASKED, nullptr);
        ValueInfo mC(assign_to.name + "xormC", 1, VProp::MASKED, nullptr);
        ValueInfo Tr3(assign_to.name + "xormTr3", 1, VProp::MASKED, nullptr);
        ValueInfo r3 = ValueInfo::getNewRand();

        region.st[r1.name] = r1;
        region.st[r2.name] = r2;
        region.st[mA.name] = mA;
        region.st[mB.name] = mB;
        region.st[mR.name] = mR;
        region.st[mT.name] = mT;
        region.st[T_.name] = T_;
        region.st[mC.name] = mC;
        region.st[Tr3.name] = Tr3;

        issueNewInst(new_insts, "^", mA, A, r1);
        issueNewInst(new_insts, "^", mB, B, r2);
        issueNewInst(new_insts, "^", mT, mA, mB);
        issueNewInst(new_insts, "^", mR, r1, r2);
        issueNewInst(new_insts, "^", T_, mT, r3);
        issueNewInst(new_insts, "^", mC, T_, mR);
        issueNewInst(new_insts, "!", Tr3, mC, ValueInfo());
        issueNewInst(new_insts, "^", assign_to, Tr3, r3);

        return new_insts;
    } else if (op == "^") {
        // XOR: T=A^B ->
        // mA=A^r1;
        // mB=B^r2;
        // mT=mA^mB;
        // mR=r1^r2;
        // T=mT^mR;

        ValueInfo r1 = ValueInfo::getNewRand();
        ValueInfo r2 = ValueInfo::getNewRand();
        ValueInfo mA(assign_to.name + "xormA", 1, VProp::MASKED, nullptr);
        ValueInfo mB(assign_to.name + "xormB", 1, VProp::MASKED, nullptr);
        ValueInfo mR(assign_to.name + "xormR", 1, VProp::MASKED, nullptr);
        ValueInfo mT(assign_to.name + "xormT", 1, VProp::MASKED, nullptr);

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
        issueNewInst(new_insts, "^", assign_to, mR, mT);

        return new_insts;
    }

    else if (op == "!" || op == "~") {
        // NOT: T=!A ->
        // mA=A^r1;
        // mT=!mA;
        // T=mT^r1

        ValueInfo r1 = ValueInfo::getNewRand();
        ValueInfo mA(assign_to.name + "notmA", 1, VProp::MASKED, nullptr);
        ValueInfo mT(assign_to.name + "notmT", 1, VProp::MASKED, nullptr);

        region.st[r1.name] = r1;
        region.st[mA.name] = mA;
        region.st[mT.name] = mT;

        issueNewInst(new_insts, "^", mA, A, r1);
        issueNewInst(new_insts, "!", mT, mA, ValueInfo());
        issueNewInst(new_insts, "^", assign_to, mT, r1);

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
        ValueInfo mA(assign_to.name + "andmA", 1, VProp::MASKED, nullptr);
        ValueInfo mB(assign_to.name + "andmB", 1, VProp::MASKED, nullptr);
        ValueInfo negmB(assign_to.name + "andneg1", 1, VProp::UNK, nullptr);
        ValueInfo mAr2(assign_to.name + "andr2", 1, VProp::UNK, nullptr);
        ValueInfo negr3(assign_to.name + "andneg2", r3.width, VProp::UNK, nullptr);
        ValueInfo tmp1(assign_to.name + "andtmp1", 1, VProp::UNK, nullptr);
        ValueInfo tmp2(assign_to.name + "andtmp2", 1, VProp::UNK, nullptr);
        ValueInfo tmp3(assign_to.name + "andtmp3", 1, VProp::UNK, nullptr);
        ValueInfo tmp4(assign_to.name + "andtmp4", 1, VProp::UNK, nullptr);
        ValueInfo tmp5(assign_to.name + "andtmp5", 1, VProp::UNK, nullptr);
        ValueInfo tmp6(assign_to.name + "andtmp6", 1, VProp::UNK, nullptr);

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
        issueNewInst(new_insts, "^", assign_to, tmp5, tmp6);

        return new_insts;
    }

    // default
    new_insts.push_back(inst);
    return new_insts;
}