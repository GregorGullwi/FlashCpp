// Test: Global struct with bitfields written via RMW path.
// Exercises emitLeaRipRelative for global bitfield stores to verify
// that REX.R is correctly set for R8-R15 destination registers.
struct Flags {
    unsigned a : 3;
    unsigned b : 5;
};

Flags g = {};

int main() {
    g.a = 3;
    g.b = 4;
    return g.a + g.b; // 3 + 4 = 7
}
