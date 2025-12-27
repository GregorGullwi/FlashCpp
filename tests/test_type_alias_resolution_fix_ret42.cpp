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

// NOTE: The following variadic recursive pattern would require complex boolean
// expressions in template arguments, which is a separate parser limitation:
// template<typename T, typename... Rest>
// struct conjunction<T, Rest...> : integral_constant<bool, T::value && conjunction<Rest...>::value> {};

int main() {
    // Test that the templates compile and instantiate correctly
    // The core feature (template parameter as base class) works!
    
    // Instantiate the templates
    conjunction<> c1;           // Uses true_type as base
    conjunction<true_type> c2;  // Uses true_type (template parameter) as base
    conjunction<false_type> c3; // Uses false_type (template parameter) as base
    
    // Return 42 to indicate successful compilation
    return 42;
}
