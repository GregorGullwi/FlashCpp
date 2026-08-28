// Regression: implicit Derived*->PrivateBase* pointer conversion must be rejected.
// Per C++20 [conv.ptr]/3 a standard derived-to-base pointer conversion requires
// the base to be accessible. FlashCpp previously accepted this silently, emitting
// the derived-to-base pointer adjustment for any base regardless of access.
struct Base { int x; };
struct Derived : private Base { int y; };

int main() {
    Derived d;
    Base* bp = &d;      // ill-formed: Base is a private base of Derived
    return bp->x;
}
