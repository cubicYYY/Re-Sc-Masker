void fiat_25519_subborrowx_u51(uint64_t* out1, fiat_25519_uint1* out2, fiat_25519_uint1 arg1, uint64_t arg2, uint64_t arg3) {
  int64_t x1;
  fiat_25519_int1 x2;
  uint64_t x3;
  x1 = ((int64_t)(arg2 - (int64_t)arg1) - (int64_t)arg3);
  x2 = (fiat_25519_int1)(x1 >> 51);
  x3 = (x1 & UINT64_C(0x7ffffffffffff));
  *out1 = x3;
  *out2 = (fiat_25519_uint1)(0x0 - x2);
}