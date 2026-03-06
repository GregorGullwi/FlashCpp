// Test: nested struct default arguments via braced-init-list
// Exercises generateDefaultStructArg with nested InitializerListNode elements.
// When the second member of the outer struct is itself a struct initialized
// with a nested brace ({...}), the codegen must recursively handle the
// InitializerListNode rather than silently emitting zero.

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    Inner inner;
};

// Default has a nested brace: {10, {12, 20}}
// inner.a = 12, inner.b = 20 => inner contributes 32
// x = 10 => total = 42
int sumOuter(Outer o = {10, {12, 20}}) {
    return o.x + o.inner.a + o.inner.b;
}

// Override the default — sanity check that explicit args still work
int sumOuterExplicit(Outer o) {
    return o.x + o.inner.a + o.inner.b;
}

int main() {
    // 1. Use the nested-struct default argument
    int r1 = sumOuter();               // 10 + 12 + 20 = 42
    if (r1 != 42) return 1;

    // 2. Override with explicit value
    Outer o2;
    o2.x = 20;
    o2.inner.a = 2;
    o2.inner.b = 20;
    int r2 = sumOuter(o2);             // 20 + 2 + 20 = 42
    if (r2 != 42) return 2;

    // 3. Explicit function (no default) — baseline
    Outer o3;
    o3.x = 5;
    o3.inner.a = 17;
    o3.inner.b = 20;
    int r3 = sumOuterExplicit(o3);     // 5 + 17 + 20 = 42
    if (r3 != 42) return 3;

    return 42;
}
