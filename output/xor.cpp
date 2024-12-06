bool masked_func(bool k1=0,bool k2=0,bool k3=0,bool t1_unmask_t1xormT=0,bool r102=0,bool r103=0,bool t1_unmask_r102=0,bool t1_allmask_1=0,bool r101=0,bool r100=0){
bool t2xormA;
bool t2xormB;
bool t2;
bool t1xormR;
bool t1;
bool t1xormT;
bool t1xormB;
bool t1xormA;
bool t2xormR;
bool t2xormT;
t1xormA = k1^r100;
t1xormB = k2^r101;
t1xormT = t1xormA^t1xormB;
t1xormR = r100^r101;
t1_allmask_1 = r102^t1xormT;
t1_unmask_r102 = t1_allmask_1^r102;
t1_unmask_t1xormT = t1_allmask_1^t1xormT;
t1 = t1xormR^t1_unmask_t1xormT;
t2xormA = t1^t1_unmask_r102;
t2xormB = k3^r103;
t2xormT = t2xormA^t2xormB;
t2xormR = r102^r103;
t2 = t2xormR^t2xormT;
return t2;
}
