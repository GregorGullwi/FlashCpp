// Test: Demonstrates the fix for type alias resolution in template arguments
// This pattern is from <type_traits> and previously failed with "Missing identifier" errors

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// Type aliases like std::true_type and std::false_type
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Template that uses type aliases as template arguments (the key fix)
// The parser now correctly resolves "false_type" and "true_type" even when
// they appear as identifiers in template argument expression contexts
template<typename... Ts>
struct conjunction;

template<>
struct conjunction<> : true_type {};  // No arguments = true

template<typename T>
struct conjunction<T> : T {};  // Single argument

template<typename T, typename... Rest>
struct conjunction<T, Rest...> : integral_constant<bool, T::value && conjunction<Rest...>::value> {};

int main() {
    // Test the conjunction logic
    constexpr bool all_true = conjunction<true_type, true_type, true_type>::value;  // Should be true (1)
    constexpr bool has_false = conjunction<true_type, false_type, true_type>::value;  // Should be false (0)
    
    // Return 42 if the logic works correctly: all_true == 1 && has_false == 0
    return (all_true && !has_false) ? 42 : 0;
}
