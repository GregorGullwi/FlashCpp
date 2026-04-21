// Regression: Derived*->PublicBase* pointer conversion must still be accepted
// after the private-base accessibility tightening (counterpart of
// test_derived_to_private_base_pointer_conv_fail.cpp).
struct Base { int x; };
struct Derived : public Base { int y; };

int main() {
    Derived d;
    d.x = 7;
    Base* bp = &d;  // well-formed: Base is a public base of Derived
    return bp->x == 7 ? 0 : 1;
}
