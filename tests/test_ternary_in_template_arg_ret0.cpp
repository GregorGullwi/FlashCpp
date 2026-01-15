// Test case: Ternary operator inside template arguments 
// This pattern is used in <ratio> header: integral_constant<int, (x < 0) ? -1 : 1>
// This test verifies that the parser correctly handles ternary operators in template arguments
// (parsing only - runtime value evaluation of ternary in template args is a separate issue)

template<int v>
struct holder {
    static constexpr int value = v;
};

// Use ternary operator in template argument (with literal condition)
// Note: The parsing now works, but ternary evaluation in template args may not be correct at runtime
using positive = holder<(5 < 0) ? -1 : 1>;
using negative = holder<(-5 < 0) ? -1 : 1>;

int main() {
    // Just verify the code compiles and runs - value correctness is not tested here
    (void)positive::value;
    (void)negative::value;
    return 0;
}
