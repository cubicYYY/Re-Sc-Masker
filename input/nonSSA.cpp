bool compute( bool k1, bool k2, bool k3){
bool z;
bool t1;
bool t2;
    t1=k1^k2;
    t1=k2^k3;
    t2=t1^k1;
    z=t2^t1;
    return z;
} 