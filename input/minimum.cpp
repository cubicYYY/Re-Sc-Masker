// Minimum

bool compute( bool k1, bool k2, bool k3){
bool n1;
bool n2;
bool n3;
bool n0;
    n3 = !k2;
    n2 = n3&k3;
    n1 = n2^k1;
    return(n1);
} 