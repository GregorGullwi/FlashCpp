// Test: ratio template with variable template specialization and if constexpr
// Verifies that namespace-qualified variable template lookup works correctly
// even when other template declarations exist between the variable template
// definition and its use. Previously, parsing any template declaration would
// reset parser state and cause qualified-type template arguments like
// std::ratio<1,2> to be misinterpreted as dependent expressions.
namespace std {
    template<long _Num, long _Den = 1>
    struct ratio {
        static constexpr long num = _Num;
        static constexpr long den = _Den;
    };

    template<typename _Tp>
    constexpr bool __is_ratio_v = false;

    template<long _Num, long _Den>
    constexpr bool __is_ratio_v<ratio<_Num, _Den>> = true;

    template<typename _R1, typename _R2>
    constexpr bool __are_both_ratios() {
        if constexpr (__is_ratio_v<_R1>)
            if constexpr (__is_ratio_v<_R2>)
                return true;
        return false;
    }
}

int main() {
    // Direct variable template checks via static_assert
    static_assert(std::__is_ratio_v<std::ratio<1,2>> == true, "ratio should match");
    static_assert(std::__is_ratio_v<int> == false, "int should not match");

    // Function template using variable template in if constexpr
    static_assert(std::__are_both_ratios<std::ratio<1,2>, std::ratio<3,4>>(), "both ratios");
    static_assert(!std::__are_both_ratios<std::ratio<1,2>, int>(), "not both ratios");

    return std::__are_both_ratios<std::ratio<1,2>, std::ratio<3,4>>() ? 1 : 0;
}
