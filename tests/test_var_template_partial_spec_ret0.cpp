// Test variable template partial specialization
template<typename T>
inline constexpr bool is_reference_v = false;

template<typename T>
inline constexpr bool is_reference_v<T&> = true;

template<typename T>
inline constexpr bool is_reference_v<T&&> = true;

// Test with array patterns
template<typename T, unsigned N>
inline constexpr unsigned extent_v = 0;

template<typename T, unsigned N>
inline constexpr unsigned extent_v<T[N], 0> = N;

int main() {
    static_assert(!is_reference_v<int>, "int should not be reference");
    static_assert(is_reference_v<int&>, "int& should be lvalue reference");
    static_assert(is_reference_v<int&&>, "int&& should be rvalue reference");
    return 0;
}
