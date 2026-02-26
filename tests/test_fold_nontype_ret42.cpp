// Test non-type parameter pack fold expression: (args | ...)
template<unsigned... args>
constexpr unsigned bitwise_or() {
    return (args | ...);
}

// Constexpr fold returning bool - verifies constexpr evaluation of && fold
template<bool... Bs>
constexpr bool all_true() {
    return (Bs && ...);
}

int main() {
    // 2 | 40 = 42
    constexpr unsigned result = bitwise_or<2, 40>();
    // Constexpr bool fold: true && true && true = true
    static_assert(all_true<true, true, true>(), "all_true should be true");
    // Constexpr bool fold used in condition
    if constexpr (!all_true<true, true>()) return 1;
    return result;  // Should return 42
}
