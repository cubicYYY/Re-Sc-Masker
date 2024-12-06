bool compute( bool k1, bool k2){
    bool n1;
    bool n2;
    bool n3;
    n3 = !k1;
    n2 = n3^k1; // true
    n1 = n2|k2;
    return(n1);
} 