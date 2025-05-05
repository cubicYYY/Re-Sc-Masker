#pragma once

#include <cassert>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Re-Sc-Masker/Preludes.hpp"
#include "Re-Sc-Masker/RegionConcatenater.hpp"
#include "Re-Sc-Masker/RegionDivider.hpp"

template <typename RegionMaskerType>
class RegionCollector;  // FIXME: remove this after the special hack to handle operator "|" is resolved

class RegionMasker : NonCopyable<RegionMasker> {};

struct RegionInOut {
    using VarSet = std::unordered_set<ValueInfo>;
    Region r;
    VarSet ins, outs;
    RegionInOut(Region &&r) : r(r){};
    RegionInOut(SymbolTable &&sym_tbl) : r(sym_tbl){};
};

template <typename Divider = TrivialRegionDivider>
class TrivialRegionMasker : RegionMasker {
public:
    TrivialRegionMasker(Divider &&divided) : global_sym_tbl(std::move(divided.global_sym_tbl)) {
        for (auto &&region : divided.regions) {
            regions_io.emplace_back(mask_one(std::move(region)));
        }
    }
    void dump() const {
        llvm::errs() << "\n(trivial masked)\n";

        for (const auto &rio : regions_io) {
            rio.r.dump();
            for (const auto &in : rio.ins) {
                llvm::errs() << in.name << "(in)\n";
            }
            for (const auto &out : rio.outs) {
                llvm::errs() << out.name << "(out)\n";
            }
        }
        llvm::errs() << "\nglobal sym tbl:\n";
        for (const auto &[varname, _] : global_sym_tbl) {
            llvm::errs() << varname << "\n";
        }
        llvm::errs() << "\n----\n";
    }

private:
    /// return a masked version of one region
    RegionInOut mask_one(Region &&originalRegion) noexcept {
        assert(originalRegion.count() == 1 || (originalRegion.dump(), false));  // trivial divider only

        RegionInOut masked_region_in_out(std::move(originalRegion.sym_tbl));

        // mask each instruction
        for (auto &&inst : originalRegion.insts) {
            if (inst.op == "//") {
                masked_region_in_out.r.insts.emplace_back(std::move(inst));
                continue;
            }

            // update in vars for this region
            masked_region_in_out.ins.insert(inst.lhs);
            if (!inst.isUnaryOp()) {
                masked_region_in_out.ins.insert(inst.rhs);
            }
            // update output vars for this region
            masked_region_in_out.outs.insert(inst.res);

            // 1 inst -> n masked insts
            mask_n_update(masked_region_in_out.r, std::move(inst));
        }
        return masked_region_in_out;
    }

    TrivialRegionMasker(TrivialRegionMasker &&other) noexcept {}
    TrivialRegionMasker &operator=(TrivialRegionMasker &&other) noexcept {
        if (this != &other) {
            regions_io = std::move(other.regions_io);
        }
        return *this;
    }

private:
    void issueNewInst(std::vector<Instruction> &newInsts, std::string_view op, const ValueInfo &t, const ValueInfo &a,
                      const ValueInfo &b) {
        newInsts.emplace_back(op, t, a, b);
    }
    void mask_n_update(Region &r, const Instruction &inst) {
        const auto &A = inst.lhs;
        const auto &B = inst.rhs;
        const auto &op = inst.op;
        const auto &res = inst.res;

        if (inst.op == "//") {
            r.insts.emplace_back(inst);
            return;
        }
        // TRICK: A|B == !( (!A) & (!B) )
        // need optimization!
        if (op == "|" || op == "||") {
            ValueInfo nA(res.name + "ornA", 1, VProp::UNK, nullptr);
            ValueInfo nB(res.name + "ornB", 1, VProp::UNK, nullptr);
            ValueInfo andNN(res.name + "orand", 1, VProp::MASKED, nullptr);
            Region temp_region;
            temp_region.sym_tbl[nA.name] = nA;
            temp_region.sym_tbl[nB.name] = nB;
            temp_region.sym_tbl[andNN.name] = andNN;

            temp_region.insts.emplace_back("!", nA, A, ValueInfo());
            temp_region.insts.emplace_back("!", nB, B, ValueInfo());
            temp_region.insts.emplace_back("&&", andNN, nA, nB);
            temp_region.insts.emplace_back("!", res, andNN, ValueInfo());

            TrivialRegionDivider real_divided(std::move(temp_region));
            TrivialRegionMasker real_masked(std::move(real_divided));
            RegionCollector real_collected(std::move(real_masked));
            RegionConcatenater real_concatenated(std::move(real_collected));

            r.sym_tbl.insert(std::make_move_iterator(real_concatenated.curRegion.sym_tbl.begin()),
                             std::make_move_iterator(real_concatenated.curRegion.sym_tbl.end()));
            r.insts.insert(r.insts.end(), std::make_move_iterator(real_concatenated.curRegion.insts.begin()),
                           std::make_move_iterator(real_concatenated.curRegion.insts.end()));
            return;
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

            r.sym_tbl[r1.name] = r1;
            r.sym_tbl[r2.name] = r2;
            r.sym_tbl[mA.name] = mA;
            r.sym_tbl[mB.name] = mB;
            r.sym_tbl[mR.name] = mR;
            r.sym_tbl[mT.name] = mT;
            r.sym_tbl[T_.name] = T_;
            r.sym_tbl[mC.name] = mC;
            r.sym_tbl[Tr3.name] = Tr3;

            issueNewInst(r.insts, "^", mA, A, r1);
            issueNewInst(r.insts, "^", mB, B, r2);
            issueNewInst(r.insts, "^", mT, mA, mB);
            issueNewInst(r.insts, "^", mR, r1, r2);
            issueNewInst(r.insts, "^", T_, mT, r3);
            issueNewInst(r.insts, "^", mC, T_, mR);
            issueNewInst(r.insts, "!", Tr3, mC, ValueInfo());
            issueNewInst(r.insts, "^", res, Tr3, r3);

            return;
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

            r.sym_tbl[r1.name] = r1;
            r.sym_tbl[r2.name] = r2;
            r.sym_tbl[mA.name] = mA;
            r.sym_tbl[mB.name] = mB;
            r.sym_tbl[mR.name] = mR;
            r.sym_tbl[mT.name] = mT;

            issueNewInst(r.insts, "^", mA, A, r1);
            issueNewInst(r.insts, "^", mB, B, r2);
            issueNewInst(r.insts, "^", mT, mA, mB);
            issueNewInst(r.insts, "^", mR, r1, r2);
            issueNewInst(r.insts, "^", res, mR, mT);

            return;
        }

        else if (op == "!" || op == "~") {
            // NOT: T=!A ->
            // mA=A^r1;
            // mT=!mA;
            // T=mT^r1

            ValueInfo r1 = ValueInfo::getNewRand();
            ValueInfo mA(res.name + "notmA", 1, VProp::MASKED, nullptr);
            ValueInfo mT(res.name + "notmT", 1, VProp::MASKED, nullptr);

            r.sym_tbl[r1.name] = r1;
            r.sym_tbl[mA.name] = mA;
            r.sym_tbl[mT.name] = mT;

            issueNewInst(r.insts, "^", mA, A, r1);
            issueNewInst(r.insts, "!", mT, mA, ValueInfo());
            issueNewInst(r.insts, "^", res, mT, r1);

            return;
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

            r.sym_tbl[r1.name] = r1;
            r.sym_tbl[r2.name] = r2;
            r.sym_tbl[r3.name] = r3;
            r.sym_tbl[mA.name] = mA;
            r.sym_tbl[mB.name] = mB;
            r.sym_tbl[negmB.name] = negmB;
            r.sym_tbl[mAr2.name] = mAr2;
            r.sym_tbl[negr3.name] = negr3;
            r.sym_tbl[tmp1.name] = tmp1;
            r.sym_tbl[tmp2.name] = tmp2;
            r.sym_tbl[tmp3.name] = tmp3;
            r.sym_tbl[tmp4.name] = tmp4;
            r.sym_tbl[tmp5.name] = tmp5;
            r.sym_tbl[tmp6.name] = tmp6;

            // Mask A and B with random values
            issueNewInst(r.insts, "^", mA, A, r1);
            issueNewInst(r.insts, "^", mB, B, r2);
            issueNewInst(r.insts, "!", negmB, mB, ValueInfo());
            issueNewInst(r.insts, "&&", mAr2, mA, r2);
            issueNewInst(r.insts, "!", negr3, r3, ValueInfo());
            issueNewInst(r.insts, "&&", tmp1, negmB, r3);
            issueNewInst(r.insts, "&&", tmp2, mB, mA);
            issueNewInst(r.insts, "!", tmp3, mAr2, ValueInfo());
            issueNewInst(r.insts, "||", tmp4, negr3, r2);
            issueNewInst(r.insts, "||", tmp5, tmp1, tmp2);
            issueNewInst(r.insts, "^", tmp6, tmp3, tmp4);
            issueNewInst(r.insts, "^", res, tmp5, tmp6);

            return;
        }

        // default
        r.insts.emplace_back(inst);
        return;
    }

public:
    std::vector<RegionInOut> regions_io;
    SymbolTable global_sym_tbl;
};

template <typename Divider>
TrivialRegionMasker(Divider &&) -> TrivialRegionMasker<std::decay_t<Divider>>;