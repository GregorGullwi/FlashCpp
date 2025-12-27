// Test: Multi-line constexpr template function with complex return type
// Mimics the pattern from <type_traits> lines 299-306

template<bool... Bs>
struct my_or {
    using type = int;
};

// Pattern from <type_traits> - multi-line constexpr function with typename return
template<typename T1, typename T2>
    constexpr typename my_or<
        true,
        false
    >::type
    test_func(T1 a, T2 b) {
    return 42;
}

int main() {
    return test_func(1, 2);  // Should return 42
}
