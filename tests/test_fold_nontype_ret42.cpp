// Test non-type parameter pack fold expression: (args | ...)
template<unsigned... args>
constexpr unsigned bitwise_or() {
    return (args | ...);
}

template<bool... Bs>
constexpr bool all_true() {
    return (Bs && ...);
}

int main() {
    // 2 | 40 = 42
    constexpr unsigned result = bitwise_or<2, 40>();
    // true && true = true  
    constexpr bool all = all_true<true, true>();
    if (!all) return 1;
    return result;  // Should return 42
}
