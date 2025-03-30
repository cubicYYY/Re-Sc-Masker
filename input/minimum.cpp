// Minimum
using my_uint1 = bool;
my_uint1 compute(my_uint1 kk1) {
  my_uint1 n0;
  my_uint1 n1;
  my_uint1 n2;
  n0 = ~kk1;
  n1 = ~n0;
  n2 = ~n1;
  return (n2);
}