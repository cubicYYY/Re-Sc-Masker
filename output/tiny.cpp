bool masked_func(bool k1=0,bool k2=0,bool k3=0,bool k4=0,bool k5=0,bool k6=0,bool n3_1=0,bool n2_1=0,bool n1_1=0,bool n0_1=0,bool n4_1=0,bool n5_1=0,bool n6_1=0,bool n7_1=0,bool n8_1=0,bool n9_1=0,bool n10_1=0,bool n11_1=0,bool temp1=0,bool temp2=0,bool temp3=0,bool temp4=0,bool temp5=0,bool temp6=0,bool temp7=0,bool r105=0,bool r103=0,bool r106=0,bool r101=0,bool r102=0,bool r104=0,bool r100=0){
bool n8_1mR;
bool n5_1mA;
bool n5_1mB;
bool n5_1mT;
bool n1_1mB;
bool n8_1mA;
bool n5_1mR;
bool n8_1mB;
bool n1_1mA;
bool n8_1mT;
bool n1_1mR;
bool n3_1mA;
bool n3_1mT;
bool n1_1mT;
n3_1mA = k2^r100;
n3_1mT = ~n3_1mA;
n3_1 = n3_1mT^k3;
n2_1 = n3_1&&r100;
n1_1mA = n2_1^r101;
n1_1mB = k1^r102;
n1_1mT = n1_1mA^n1_1mB;
n1_1mR = r101^r102;
n1_1 = n1_1mR^k4;
n0_1 = n1_1||n1_1mT;
n4_1 = n0_1&&k5;
n5_1mA = n4_1^r103;
n5_1mB = k6^r104;
n5_1mT = n5_1mA^n5_1mB;
n5_1mR = r103^r104;
n5_1 = n5_1mR^n4_1;
temp1 = n5_1||n5_1mT;
temp2 = n0_1||k1;
n6_1 = temp1&&temp2;
temp4 = n4_1&&n2_1;
n7_1 = temp3||temp4;
n8_1mA = n7_1^r105;
n8_1mB = n1_1^r106;
n8_1mT = n8_1mA^n8_1mB;
n8_1mR = r105^r106;
n8_1 = n8_1mR^k3;
temp5 = n8_1||n8_1mT;
n9_1 = n0_1&&temp5;
temp6 = n2_1&&n4_1;
n10_1 = n9_1||temp6;
temp7 = n6_1||k5;
n11_1 = n10_1&&temp7;
return n11_1;
}
