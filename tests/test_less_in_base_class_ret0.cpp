// Test for less-than vs template argument disambiguation in base classes
// The pattern `integral_constant<bool, _R1::num < _R2::num>` should correctly parse
// the < as a comparison operator, not as template arguments for `num`

template<bool B>
struct bool_constant {
    static constexpr bool value = B;
};

template<typename T>
struct R1 {
    static constexpr T num = 10;
};

template<typename T>
struct R2 {
    static constexpr T num = 5;
};

// This pattern previously failed - comparison in template argument in base class
template<typename T>
struct ratio_less
    : bool_constant<R1<T>::num < R2<T>::num>   // < is comparison, not template args
    { };

// Also test with parentheses (should also work)
template<typename T>
struct ratio_less_paren
    : bool_constant<(R1<T>::num < R2<T>::num)>
    { };

int main() {
    // Verify the template can be instantiated and return based on template value
    // ratio_less<long>::value should be false (10 < 5 is false), so return 0
    return ratio_less<long>::value ? 1 : 0;
}
