// Minimum
using my_uint2 = bool;
my_uint2 compute(my_uint2 kk1) {
  my_uint2 n0;
  my_uint2 n1;
  my_uint2 n2;
  n0 = ~kk1;
  n1 = !n0;
  n2 = ~n1;
  return (n2);
}