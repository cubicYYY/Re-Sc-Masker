bool xorf(bool k1, bool k2, bool k3) {
    bool t1;
    bool t2;

    t1 = k1 ^ k2;
    t2 = t1 ^ k3;

    return t2;
}