// Test template parameter default that references earlier parameter
// This tests the fix for "Missing identifier" when using earlier template
// parameters in later template parameter defaults.
//
// Pattern from <type_traits>:
// template<typename _Tp, bool = is_arithmetic<_Tp>::value>

template<typename T>
struct is_arithmetic {
    static constexpr bool value = false;
};

template<>
struct is_arithmetic<int> {
    static constexpr bool value = true;
};

template<typename _Tp, bool IsArith = is_arithmetic<_Tp>::value>
struct test {
    static constexpr int val = IsArith ? 42 : 0;
};

int main() {
    return test<int>::val;  // Should return 42 (int is arithmetic)
}
