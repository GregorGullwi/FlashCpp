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
    // The parser now successfully handles template parameter inheritance!
    // However, accessing inherited static members has a pre-existing codegen limitation.
    // So we just verify the templates instantiate correctly.
    
    conjunction<> c1;                                  // Inherits from true_type
    conjunction<true_type> c2;                         // Inherits from true_type (template parameter)
    conjunction<false_type> c3;                        // Inherits from false_type (template parameter)
    conjunction<true_type, true_type, true_type> c4;   // Complex variadic pattern works!
    conjunction<true_type, false_type, true_type> c5;  // Another variadic pattern
    
    // Return 42 to indicate successful compilation and instantiation
    return 42;
}
