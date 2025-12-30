// Even simpler test to debug the register flush issue
// Just test the logical not and branch

template<typename T, bool v>
struct integral_constant { static constexpr bool value = v; };

template<typename T, typename U>
struct is_same : integral_constant<bool, false> {};

template<typename T>
struct is_same<T, T> : integral_constant<bool, true> {};

int main() {
    // Test that is_same<int, int>::value is true (1)
    if (is_same<int, int>::value) {
        return 10;  // Should execute
    }
    return 20;  // Should not execute
}
