// Test: static member pointer, reference, const, and constexpr qualifiers
// Verifies that static members with various qualifiers parse and codegen correctly.

struct Bar {
    static const int cval;
};

const int Bar::cval = 10;

struct Foo {
    static constexpr int A = 20;
    static constexpr const int B = 22;
    static int* ptr;
    static int& ref;
    static const int* cptr;
};

int x = 99;
int* Foo::ptr = &x;

int main() {
    // constexpr + const out-of-class + sizeof on static/non-static members
    int result = Foo::A + Foo::B + Bar::cval;  // 20 + 22 + 10 = 52
    result -= 10;                               // 42
    return result;
}
