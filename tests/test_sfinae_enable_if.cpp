// Test SFINAE (Substitution Failure Is Not An Error) with enable_if
// This tests the compiler's ability to handle template specializations
// when accessing type members that may not exist

// Simple enable_if implementation - the key SFINAE pattern
// Primary template has NO 'type' member
template<bool B, typename T = void>
struct enable_if {
    // No 'type' member when B is false - this causes substitution failure
};

// Specialization for B=true HAS 'type' member
template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// Type trait for testing
template<typename T>
struct is_int {
    static constexpr bool value = false;
};

template<>
struct is_int<int> {
    static constexpr bool value = true;
};

// Test function: only enabled when is_int<T>::value is true
// If is_int<T>::value is false, enable_if<false>::type fails (no such member)
// This should trigger SFINAE - the function template is silently removed from overload set
template<typename T>
typename enable_if<is_int<T>::value, int>::type
only_for_int(T val) {
    return val + 100;
}

int main() {
    // This should work - is_int<int>::value is true
    int result = only_for_int(42);
    
    // This would fail if uncommented - is_int<double>::value is false
    // The compiler should give an error about no matching function
    // double d_result = only_for_int(3.14);
    
    // Expected: 42 + 100 = 142
    return (result == 142) ? 0 : 1;
}
