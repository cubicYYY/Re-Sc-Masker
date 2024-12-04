bool masked_func(bool k1=0,bool k2=0,bool k3=0,bool n1=0,bool n2=0,bool n3=0,bool r104=0,bool r101=0,bool r105=0,bool r103=0,bool r102=0,bool r100=0){
bool n1mT;
bool n2tmp3;
bool n1mA;
bool n2tmp1;
bool n1mB;
bool n3mAr2;
bool k3mBneg;
bool r103neg;
bool k3mB;
bool n2tmp6;
bool n3mT;
bool n3mA;
bool n1mR;
bool n2tmp2;
bool n2tmp4;
bool n2tmp5;
n3mA = k2^r100;
n3mT = ~n3mA;
n3 = n3mT^r101;

n3mA = n3^r100;
k3mB = k3^r102;
k3mBneg = ~k3mB;
n3mAr2 = n3mA&r102;
r103neg = ~r103;
n2tmp1 = k3mBneg&r103;
n2tmp2 = k3mB&n3mA;
n2tmp3 = ~n3mAr2;
n2tmp4 = r103neg|r102;
n2tmp5 = n2tmp1|n2tmp2;
n2tmp6 = n2tmp3^n2tmp4;
n2 = n2tmp5^r104;

n1mA = n2^n2tmp6;
n1mB = k1^r105;
n1mT = n1mA^n1mB;
n1mR = r104^r105;
n1 = n1mR^n1mT;
return n1;
}
