// Test Undefined Behavior in constexpr

constexpr int div_by_zero(int a, int b) {
    return a / b;
}

// This should fail compilation
// constexpr int fail_div = div_by_zero(10, 0);

constexpr int signed_overflow(int a) {
    return a + 1;
}

// This should fail compilation (assuming 32-bit int)
// constexpr int max_int = 2147483647;
// constexpr int fail_overflow = signed_overflow(max_int);

constexpr int shift_too_much(int a) {
    return a << 33;
}

// This should fail compilation
// constexpr int fail_shift = shift_too_much(1);
