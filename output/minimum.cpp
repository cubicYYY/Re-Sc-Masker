bool masked_func(bool kk1=0,bool r12=0,bool r11=0,bool r10=0){
bool n2notmA;
bool n2notmT;
bool n1notmA;
bool n0notmA;
bool n1notmT;
bool n0notmT;
n0notmA = kk1^r10;
n0notmT = !n0notmA;
//def:
n0 = n0notmT^r11;
//------region-----
n1notmA = n0^r10;
n1notmT = !n1notmA;
//def:
n1 = n1notmT^r12;
//------region-----
n2notmA = n1^r11;
n2notmT = !n2notmA;
//def:
n2 = n2notmT^r12;
//------region-----
return n2;
}
