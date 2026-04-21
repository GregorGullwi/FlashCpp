// Regression: `this == &other` (pointer-to-struct equality) must compile and
// behave as a builtin pointer comparison per C++20 [expr.eq]/3. Previously the
// IR generator misrouted pointer comparisons through the user-defined operator
// lookup path because its pointer_depth check was based on a TypeSpecifierNode
// that occasionally arrived with pointer_depth==0 (e.g. for `this`), causing
// "Operator== not defined for operand types" errors while compiling any
// self-assignment guard in MSVC <typeinfo>/<exception> headers.
struct Foo {
    int x;
    Foo& assign(const Foo& other) {
        if (this == &other) {
            return *this;
        }
        x = other.x;
        return *this;
    }
};

int main() {
    Foo a; a.x = 7;
    Foo b; b.x = 3;
    a.assign(b);
    if (a.x != 3) return 1;

    // Self-assignment guard path must short-circuit.
    a.assign(a);
    if (a.x != 3) return 2;

    // Different-pointer compare must evaluate unequal.
    if (&a == &b) return 3;
    if (!(&a != &b)) return 4;

    return 0;
}
