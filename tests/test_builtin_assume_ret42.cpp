// Test __builtin_assume intrinsic
// This is an optimization hint that a condition is true

int main() {
    int x = 42;
    __builtin_assume(x == 42);  // Tell compiler x is 42
    // In optimized code, compiler could use this assumption
    return x;  // Expected: 42
}
