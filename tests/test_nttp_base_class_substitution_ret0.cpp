// Test that non-type template parameters are correctly substituted when
// passed through inheritance chains to integral_constant-like patterns.
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

template<unsigned long long N>
struct extent_helper : integral_constant<unsigned long long, N> {};

template<int V>
struct int_constant : integral_constant<int, V> {};

int main() {
    // Test non-type param through inheritance with unsigned long long
    bool ok1 = extent_helper<42>::value == 42;
    // Test non-type param through inheritance with int
    bool ok2 = int_constant<7>::value == 7;
    // Test zero value
    bool ok3 = extent_helper<0>::value == 0;
    return (ok1 && ok2 && ok3) ? 0 : 1;
}
