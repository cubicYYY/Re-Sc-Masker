bool masked_func(bool k1=0,bool k2=0,bool k3=0,bool t1=0,bool r104=0,bool r100=0,bool r103=0,bool r101=0,bool r102=0,bool r105=0){
bool t1orandandmA;
bool t1orandandtmp1;
bool t1orandandtmp6;
bool t1notmT;
bool t1orandandneg2;
bool t1orandandmB;
bool t1orandandtmp4;
bool t1orandandtmp5;
bool t1ornAnotmA;
bool t1ornBnotmT;
bool t1ornBnotmA;
bool t1ornB;
bool t1notmA;
bool t1orand;
bool t1orandandtmp2;
bool t1orandandr2;
bool t1ornAnotmT;
bool t1orandandtmp3;
bool t1orandandneg1;
bool t1ornA;
t1ornAnotmA = k1^r100;
t1ornAnotmT = !t1ornAnotmA;
t1ornA = t1ornAnotmT^r102;
t1ornBnotmA = k2^r101;
t1ornBnotmT = !t1ornBnotmA;
t1ornB = t1ornBnotmT^r103;
t1orandandmA = t1ornA^r100;
t1orandandmB = t1ornB^r101;
t1orandandneg1 = !t1orandandmB;
t1orandandr2 = t1orandandmA&&r103;
t1orandandneg2 = !r104;
t1orandandtmp1 = t1orandandneg1&&r104;
t1orandandtmp2 = t1orandandmB&&t1orandandmA;
t1orandandtmp3 = !t1orandandr2;
t1orandandtmp4 = t1orandandneg2||r103;
t1orandandtmp5 = t1orandandtmp1||t1orandandtmp2;
t1orandandtmp6 = t1orandandtmp3^t1orandandtmp4;
t1orand = t1orandandtmp5^r105;
t1notmA = t1orand^t1orandandtmp6;
t1notmT = !t1notmA;
t1 = t1notmT^r105;
return t1;
}
