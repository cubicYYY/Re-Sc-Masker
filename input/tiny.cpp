// Tiny


bool tiny_compute(bool k1, bool k2, bool k3, bool k4, bool k5, bool k6) {
    bool n3_1;
    bool n2_1;
    bool n1_1;
    bool n0_1;
    bool n4_1;
    bool n5_1;
    bool n6_1;
    bool n7_1;
    bool n8_1;
    bool n9_1;
    bool n10_1;
    bool n11_1;
    bool temp1;
    bool temp2;
    bool temp3;
    bool temp4;
    bool temp5;
    bool temp6;
    bool temp7;


    n3_1 = ~k2;

    n2_1 = n3_1 & k3;
    n1_1 = n2_1 ^ k1;
    n0_1 = n1_1 | k4;

    n4_1 = n0_1 & k5;
    n5_1 = n4_1 ^ k6;

    temp1 = n5_1 | n4_1;
    temp2 = n0_1 | k1;
    n6_1 = temp1 & temp2;
    n7_1 = temp3;
    temp4 = n4_1 & n2_1;
    n7_1 = temp3 | temp4;

    n8_1 = n7_1 ^ n1_1;
    temp5 = n8_1 | k3;
    n9_1 = n0_1 & temp5;
    temp6 = n2_1 & n4_1;
    n10_1 = n9_1 | temp6;
    temp7 = n6_1 | k5;
    n11_1 = n10_1 & temp7;

    return n11_1;
}