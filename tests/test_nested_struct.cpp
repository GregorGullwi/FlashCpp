// Test nested struct alignment

struct Inner {
    char c;   // offset 0
    int i;    // offset 4 (3 bytes padding)
};  // total size 8, alignment 4

struct Outer {
    char c;      // offset 0
    Inner inner; // offset 4 (3 bytes padding, aligned to 4)
    int i;       // offset 12 (no padding needed)
};  // total size 16, alignment 4

int test() {
    Inner inner;
    inner.c = 10;
    inner.i = 20;

    Outer o;
    o.c = 5;
    o.i = 30;

    // Test accessing nested struct member (read only, no assignment to o.inner yet)
    // Just return a simple value for now
    return o.i;  // Should be 30
}

