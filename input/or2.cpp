bool tiny_compute(bool k1, bool k2, bool k3 = 0) {
  bool t1;
  bool t2;

  t1 = k1 | k2;
  t2 = t1 | t1;

  return t2;
}