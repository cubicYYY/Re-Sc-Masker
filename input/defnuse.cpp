bool compute( bool k1, bool k2, bool k3, bool r1, bool r2, bool r3){
bool x;
bool y;
bool z;
bool t1;
bool t2;
    t1=!k1;
    t2=t1^r1;
    x=t1^r2;
    y=t2^r3;
    z=x^y;
    return z;
} 