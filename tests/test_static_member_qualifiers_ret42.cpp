// Test: static member pointer, reference, const, and constexpr qualifiers
// Verifies that static members with various qualifiers parse and codegen correctly.

struct Bar {
    static const int cval;
};

const int Bar::cval = 10;

int x = 99;

struct Foo {
    static constexpr int A = 3;
    static int* ptr;
    static int& ref;
};

int* Foo::ptr = &x;
int& Foo::ref = x;

int main() {
    int result = Foo::A;            // 3
    result += *Foo::ptr;            // 3 + 99 = 102
    result -= Foo::ref;             // 102 - 99 = 3
    result += Bar::cval;            // 3 + 10 = 13
    result += *Foo::ptr - Foo::ref; // 13 + (99 - 99) = 13
    result += 29;                   // 42
    return result;
}
