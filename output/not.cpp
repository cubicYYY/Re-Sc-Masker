bool masked_func(bool k1=0,bool r100=0,bool r101=0){
bool t1_unmask_r101;
bool t1_unmask_r100;
bool t1_allmask_1;
bool t2notmT;
bool t1notmA;
bool t1notmT;
bool t2notmA;
bool t1;
bool t2;
t1notmA = k1^r100;
t1notmT = !t1notmA;
t1_allmask_1 = r101^r100;
t1_unmask_r101 = t1_allmask_1^r101;
t1_unmask_r100 = t1_allmask_1^r100;
t1 = t1notmT^t1_unmask_r100;
t2notmA = t1^t1_unmask_r101;
t2notmT = !t2notmA;
t2 = t2notmT^r101;
return t2;
}
