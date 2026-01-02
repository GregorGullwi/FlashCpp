// Test cv-qualified partial specialization (T const, T volatile patterns)
// This test verifies that FlashCpp supports postfix cv-qualifiers in template arguments
// which is required for standard library headers like <type_traits>

template<typename T>
struct is_const {
    static constexpr bool value = false;
};

template<typename T>
struct is_const<T const> {
    static constexpr bool value = true;
};

template<typename T>
struct is_volatile {
    static constexpr bool value = false;
};

template<typename T>
struct is_volatile<T volatile> {
    static constexpr bool value = true;
};

int main() {
    // is_const<int>::value should be 0
    // is_const<const int>::value should be 1
    // is_volatile<int>::value should be 0
    // is_volatile<volatile int>::value should be 1
    int a = is_const<int>::value;           // 0
    int b = is_const<const int>::value;     // 1
    int c = is_volatile<int>::value;        // 0
    int d = is_volatile<volatile int>::value; // 1
    return a + b + c + d; // Should return 2
}
