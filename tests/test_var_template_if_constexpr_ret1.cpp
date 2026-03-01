// Test variable template used in if constexpr inside function template
// Verifies that variable template partial specializations are correctly
// evaluated during constexpr evaluation in template instantiation.

template<int N, int D>
struct ratio {
    static constexpr int num = N;
    static constexpr int den = D;
};

template<typename T>
constexpr bool is_ratio_v = false;

template<int N, int D>
constexpr bool is_ratio_v<ratio<N, D>> = true;

template<typename R1, typename R2>
constexpr bool are_both_ratios() {
    if constexpr (is_ratio_v<R1>)
        if constexpr (is_ratio_v<R2>)
            return true;
    return false;
}

int main() {
    static_assert(are_both_ratios<ratio<1, 2>, ratio<2, 4>>(), "both should be ratios");
    static_assert(!are_both_ratios<ratio<1, 2>, int>(), "int is not a ratio");
    return are_both_ratios<ratio<1, 2>, ratio<2, 4>>() ? 1 : 0;
}
