// Test with logical NOT
template<typename T, bool v>
struct integral_constant { static constexpr bool value = v; };

template<typename T, typename U>
struct is_same : integral_constant<bool, false> {};

template<typename T>
struct is_same<T, T> : integral_constant<bool, true> {};

int main() {
    // Test that !is_same<int, int>::value is false (0)
    if (!is_same<int, int>::value) {
        return 20;  // Should NOT execute
    }
    return 10;  // Should execute
}
