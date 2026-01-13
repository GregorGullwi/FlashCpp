// Test fold expression evaluation in static member initializers
// This is needed for standard library headers like <utility> that use
// patterns like: static constexpr bool value = (Bs && ...);

template<bool... Bs>
struct __and_ {
    static constexpr bool value = (Bs && ...);
};

template<bool... Bs>
struct __or_ {
    static constexpr bool value = (Bs || ...);
};

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> { using type = T; };

// Use __and_<true, true>::value in enable_if (pattern from <utility>)
template<typename T>
typename enable_if<__and_<true, true>::value, T>::type
test_and_func(T x) {
    return x;
}

// Use __or_<false, true>::value in enable_if
template<typename T>
typename enable_if<__or_<false, true>::value, T>::type
test_or_func(T x) {
    return x;
}

// Test noexcept with fold expression
template<typename T>
void swap_test(T& a, T& b) noexcept(__and_<true, true>::value) {
    T tmp = a;
    a = b;
    b = tmp;
}

int main() {
    // Test __and_<true, true> = true
    int result1 = test_and_func(42);
    if (result1 != 42) return 1;
    
    // Test __or_<false, true> = true
    int result2 = test_or_func(42);
    if (result2 != 42) return 2;
    
    // Test swap with noexcept
    int x = 1, y = 2;
    swap_test(x, y);
    if (x != 2 || y != 1) return 3;
    
    return 0;
}
