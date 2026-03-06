// Test: parameter pack after default argument is valid per C++20
// Per C++20 [dcl.fct.default]/4: "...unless the parameter was expanded
// from a parameter pack or is a function parameter pack."
// A function parameter pack following a defaulted parameter must not
// be rejected by the parser.

template<typename... Args>
int sum_pack(int base = 42, Args... args) {
    int total = base;
    // Fold expression to sum all pack args
    ((total += args), ...);
    return total;
}

int main() {
    // Call with only the default — pack is empty
    int r1 = sum_pack();                 // base=42, pack={}  → 42
    if (r1 != 42) return 1;

    // Call with explicit base, empty pack
    int r2 = sum_pack(42);               // base=42, pack={}  → 42
    if (r2 != 42) return 2;

    // Call with explicit base and pack args that sum to zero
    int r3 = sum_pack(40, 1, 1);         // base=40, pack={1,1}  → 42
    if (r3 != 42) return 3;

    return 42;
}
