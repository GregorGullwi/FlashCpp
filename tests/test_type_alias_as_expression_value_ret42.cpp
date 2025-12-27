// Test: Type aliases should be usable as standalone values in expressions
// This tests a specific form where the type alias itself is treated as a value

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    constexpr operator value_type() const { return value; }
};

// Create type aliases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Test function that tries to use type alias as a value
// This might be the pattern that fails
template<typename T>
constexpr bool get_value() {
    // Try to construct from type alias - this is like bool(false_type{})
    // In standard library, this would be something like:
    // return bool(is_void<T>::value)  // where ::value resolves to a type alias sometimes
    return bool(false_type::value);
}

int main() {
    constexpr bool result = get_value<int>();  
    return result ? 1 : 42;  // Should return 42 since result is false
}
