#include "Re-Sc-Masker/RegionMasker.hpp"

#include <llvm-16/llvm/Support/raw_ostream.h>

#include <iterator>

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
    region.sym_tbl = originalRegion.sym_tbl;
    // No instruments initially
    for (auto &&inst : originalRegion.insts) {
        if (inst.op == "//") {
            region.insts.emplace_back(inst);
            continue;
        }
        auto &&maskedInsts = replaceInstruction(inst);  // 1->n
        region.insts.insert(region.insts.end(), std::make_move_iterator(maskedInsts.begin()),
                            std::make_move_iterator(maskedInsts.end()));
        outputs.insert(inst.res);
    }
}

TrivialRegionMasker::TrivialRegionMasker(TrivialRegionMasker &&other) noexcept
    : region(std::move(other.region)), outputs(std::move(other.outputs)) {}

TrivialRegionMasker &TrivialRegionMasker::operator=(TrivialRegionMasker &&other) noexcept {
    if (this != &other) {
        region = std::move(other.region);
        outputs = std::move(other.outputs);
    }
    return *this;
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
    const auto &res = inst.res;

    // TODO: avoid naming conflicts
    if (inst.op == "//") {
        new_insts.emplace_back(inst);
        return new_insts;
    }
    // TRICK: A|B == !( (!A) & (!B) )
    // need optimization!
    if (op == "|" || op == "||") {
        ValueInfo nA(res.name + "ornA", 1, VProp::UNK, nullptr);
        ValueInfo nB(res.name + "ornB", 1, VProp::UNK, nullptr);
        ValueInfo andNN(res.name + "orand", 1, VProp::MASKED, nullptr);
        region.sym_tbl[nA.name] = nA;
        region.sym_tbl[nB.name] = nB;
        region.sym_tbl[andNN.name] = andNN;

        // Do not call issueNewInst since we dont want side effects in the future
        new_insts.emplace_back("!", nA, A, ValueInfo());
        new_insts.emplace_back("!", nB, B, ValueInfo());
        new_insts.emplace_back("&&", andNN, nA, nB);
        new_insts.emplace_back("!", res, andNN, ValueInfo());
        Region realReplaced;
        realReplaced.insts = new_insts;

        TrivialRegionDivider realDivided(realReplaced);

        RegionCollector<TrivialRegionMasker> defuse;
        while (!realDivided.done()) {
            Region subRegion = realDivided.next();
            defuse.add(TrivialRegionMasker(subRegion));
        }

        RegionConcatenater res{std::move(defuse)};
        region.sym_tbl.insert(res.curRegion.sym_tbl.begin(), res.curRegion.sym_tbl.end());

        return res.curRegion.insts;
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
        ValueInfo mA(res.name + "xormA", 1, VProp::MASKED, nullptr);
        ValueInfo mB(res.name + "xormB", 1, VProp::MASKED, nullptr);
        ValueInfo mR(res.name + "xormR", 1, VProp::MASKED, nullptr);
        ValueInfo mT(res.name + "xormT", 1, VProp::MASKED, nullptr);
        ValueInfo T_(res.name + "xormT_", 1, VProp::MASKED, nullptr);
        ValueInfo mC(res.name + "xormC", 1, VProp::MASKED, nullptr);
        ValueInfo Tr3(res.name + "xormTr3", 1, VProp::MASKED, nullptr);
        ValueInfo r3 = ValueInfo::getNewRand();

        region.sym_tbl[r1.name] = r1;
        region.sym_tbl[r2.name] = r2;
        region.sym_tbl[mA.name] = mA;
        region.sym_tbl[mB.name] = mB;
        region.sym_tbl[mR.name] = mR;
        region.sym_tbl[mT.name] = mT;
        region.sym_tbl[T_.name] = T_;
        region.sym_tbl[mC.name] = mC;
        region.sym_tbl[Tr3.name] = Tr3;

        issueNewInst(new_insts, "^", mA, A, r1);
        issueNewInst(new_insts, "^", mB, B, r2);
        issueNewInst(new_insts, "^", mT, mA, mB);
        issueNewInst(new_insts, "^", mR, r1, r2);
        issueNewInst(new_insts, "^", T_, mT, r3);
        issueNewInst(new_insts, "^", mC, T_, mR);
        issueNewInst(new_insts, "!", Tr3, mC, ValueInfo());
        issueNewInst(new_insts, "^", res, Tr3, r3);

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
        ValueInfo mA(res.name + "xormA", 1, VProp::MASKED, nullptr);
        ValueInfo mB(res.name + "xormB", 1, VProp::MASKED, nullptr);
        ValueInfo mR(res.name + "xormR", 1, VProp::MASKED, nullptr);
        ValueInfo mT(res.name + "xormT", 1, VProp::MASKED, nullptr);

        region.sym_tbl[r1.name] = r1;
        region.sym_tbl[r2.name] = r2;
        region.sym_tbl[mA.name] = mA;
        region.sym_tbl[mB.name] = mB;
        region.sym_tbl[mR.name] = mR;
        region.sym_tbl[mT.name] = mT;

        issueNewInst(new_insts, "^", mA, A, r1);
        issueNewInst(new_insts, "^", mB, B, r2);
        issueNewInst(new_insts, "^", mT, mA, mB);
        issueNewInst(new_insts, "^", mR, r1, r2);
        issueNewInst(new_insts, "^", res, mR, mT);

        return new_insts;
    }

    else if (op == "!" || op == "~") {
        // NOT: T=!A ->
        // mA=A^r1;
        // mT=!mA;
        // T=mT^r1

        ValueInfo r1 = ValueInfo::getNewRand();
        ValueInfo mA(res.name + "notmA", 1, VProp::MASKED, nullptr);
        ValueInfo mT(res.name + "notmT", 1, VProp::MASKED, nullptr);

        region.sym_tbl[r1.name] = r1;
        region.sym_tbl[mA.name] = mA;
        region.sym_tbl[mT.name] = mT;

        issueNewInst(new_insts, "^", mA, A, r1);
        issueNewInst(new_insts, "!", mT, mA, ValueInfo());
        issueNewInst(new_insts, "^", res, mT, r1);

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
        ValueInfo mA(res.name + "andmA", 1, VProp::MASKED, nullptr);
        ValueInfo mB(res.name + "andmB", 1, VProp::MASKED, nullptr);
        ValueInfo negmB(res.name + "andneg1", 1, VProp::UNK, nullptr);
        ValueInfo mAr2(res.name + "andr2", 1, VProp::UNK, nullptr);
        ValueInfo negr3(res.name + "andneg2", r3.width, VProp::UNK, nullptr);
        ValueInfo tmp1(res.name + "andtmp1", 1, VProp::UNK, nullptr);
        ValueInfo tmp2(res.name + "andtmp2", 1, VProp::UNK, nullptr);
        ValueInfo tmp3(res.name + "andtmp3", 1, VProp::UNK, nullptr);
        ValueInfo tmp4(res.name + "andtmp4", 1, VProp::UNK, nullptr);
        ValueInfo tmp5(res.name + "andtmp5", 1, VProp::UNK, nullptr);
        ValueInfo tmp6(res.name + "andtmp6", 1, VProp::UNK, nullptr);

        region.sym_tbl[r1.name] = r1;
        region.sym_tbl[r2.name] = r2;
        region.sym_tbl[r3.name] = r3;
        region.sym_tbl[mA.name] = mA;
        region.sym_tbl[mB.name] = mB;
        region.sym_tbl[negmB.name] = negmB;
        region.sym_tbl[mAr2.name] = mAr2;
        region.sym_tbl[negr3.name] = negr3;
        region.sym_tbl[tmp1.name] = tmp1;
        region.sym_tbl[tmp2.name] = tmp2;
        region.sym_tbl[tmp3.name] = tmp3;
        region.sym_tbl[tmp4.name] = tmp4;
        region.sym_tbl[tmp5.name] = tmp5;
        region.sym_tbl[tmp6.name] = tmp6;

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
        issueNewInst(new_insts, "^", res, tmp5, tmp6);

        return new_insts;
    }

    // default
    new_insts.emplace_back(inst);
    return new_insts;
}