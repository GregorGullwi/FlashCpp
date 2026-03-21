// Test: constexpr, constinit, and static constexpr variables store hardcoded constant values

// Global constexpr - should be stored in .rodata as a compile-time constant
constexpr int g_val = 42;

// Global constinit - initialized with a constant expression but stored in .data (mutable at runtime)
constexpr int g_base = 58;
constinit int g_init = g_base;

// Struct with static constexpr member - should be stored in .rodata
struct Constants {
    static constexpr int max_val = 100;
    static constexpr float pi_approx = 3;  // simplified float value (3.0) for easy testing
    static constexpr long long big_val = 1000000LL;
};

int main() {
    // Local constexpr - value is hardcoded at compile time, allocated on stack with constant init
    constexpr int local_val = 7;

    // Static local constexpr - stored in .rodata section as a global constant
    static constexpr int static_local = 50;

    // Verify all values are correct
    int sum = g_val + g_init + Constants::max_val + local_val + static_local;
    // 42 + 58 + 100 + 7 + 50 = 257

    if (sum != 257) return 1;

    // Verify struct constants work
    if (Constants::max_val != 100) return 2;
    if (Constants::big_val != 1000000LL) return 3;

    // Verify constinit is mutable at runtime
    g_init += 1;  // constinit allows runtime modification
    if (g_init != g_base + 1) return 4;

    return 0;
}
