bool extended_compute(bool k1, bool k2, bool k3, bool k4, bool k5, bool k6, bool k7, bool k8, bool k9, bool k10, bool k11, bool k12, bool k13, bool k14, bool k15, bool k16, bool k17, bool k18, bool k19, bool k20, bool p1, bool p2, bool p3, bool p4, bool p5, bool p6, bool p7, bool p8, bool p9, bool p10, bool p11, bool p12, bool p13, bool p14, bool p15, bool p16, bool p17, bool p18, bool p19, bool p20, bool p21, bool p22, bool p23, bool p24, bool p25, bool p26, bool p27, bool p28, bool p29, bool p30, bool p31, bool p32, bool p33, bool p34, bool p35, bool p36, bool p37, bool p38, bool p39, bool p40) {
    bool n3_1;
    bool n2_1;
    bool n1_1;
    bool n0_1;
    bool n4_1;
    bool n5_1;

    bool temp1;
    bool temp2;
    bool n6_1;
    bool temp3;
    bool n7_1;

    bool temp4;
    bool temp5;
    bool n8_1;

    bool n9_1;
    bool temp6;
    bool temp7;
    bool n10_1;

    bool temp8;
    bool n11_1;
    bool n12_1;

    bool temp9;
    bool n13_1;
    bool temp10;
    bool n14_1;

    bool temp11;
    bool n15_1;
    bool temp12;
    bool n16_1;

    bool temp13;
    bool n17_1;
    bool temp14;
    bool n18_1;

    bool temp15;
    bool n19_1;
    bool temp16;
    bool n20_1;

    bool temp17;
    bool n21_1;
    bool temp18;
    bool n22_1;

    n3_1 = k1 & k2;
    n2_1 = p1 & n3_1;
    n1_1 = p2 ^ n2_1;
    n0_1 = p3 | n1_1;
    n4_1 = p4 & n0_1;
    n5_1 = p5 ^ n4_1;

    temp1 = p6 | n5_1;
    temp2 = p7 & n3_1;
    n6_1 = p8 & temp1;
    temp3 = p9 ^ n6_1;
    n7_1 = p10 | temp3;

    temp4 = p11 & n7_1;
    temp5 = p12 | temp4;
    n8_1 = p13 ^ temp5;

    n9_1 = p14 & n8_1;
    temp6 = p15 | n9_1;
    temp7 = p16 & n3_1;
    n10_1 = p17 ^ temp6;

    temp8 = p18 | n10_1;
    n11_1 = p19 & temp8;
    n12_1 = p20 ^ n11_1;

    temp9 = p21 | n12_1;
    n13_1 = p22 & temp9;
    temp10 = p23 ^ n13_1;
    n14_1 = p24 | temp10;

    temp11 = p25 & n14_1;
    n15_1 = p26 ^ temp11;
    temp12 = p27 | n15_1;
    n16_1 = p28 & temp12;

    temp13 = p29 ^ n16_1;
    n17_1 = p30 | temp13;
    temp14 = p31 & n17_1;
    n18_1 = p32 ^ temp14;

    temp15 = p33 | n18_1;
    n19_1 = p34 & temp15;
    temp16 = p35 ^ n19_1;
    n20_1 = p36 | temp16;

    temp17 = p37 & n20_1;
    n21_1 = p38 ^ temp17;
    temp18 = p39 | n21_1;
    n22_1 = p40 & temp18;

    return n22_1;
}
