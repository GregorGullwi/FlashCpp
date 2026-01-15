// Test case: Ternary operator inside template arguments 
// This pattern is used in <ratio> header: integral_constant<int, (x < 0) ? -1 : 1>
// This test verifies that the parser correctly handles ternary operators in template arguments
// AND that the ternary is correctly evaluated at compile time for runtime use.

template<int v>
struct holder {
    static constexpr int value = v;
};

// Use ternary operator in template argument (with literal condition)
// (5 < 0) is false, so positive::value should be 1
// (-5 < 0) is true, so negative::value should be -1
using positive = holder<(5 < 0) ? -1 : 1>;
using negative = holder<(-5 < 0) ? -1 : 1>;

int main() {
    // Verify both values are correct at runtime
    // positive::value should be 1 (since 5 < 0 is false)
    // negative::value should be -1 (since -5 < 0 is true)
    // If both are correct: 1 + (-1) = 0
    // If either is wrong, return will be non-zero
    return positive::value + negative::value;
}
