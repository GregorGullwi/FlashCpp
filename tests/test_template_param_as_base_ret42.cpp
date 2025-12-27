// Test: Template parameter used as base class
// This tests the advanced C++20 pattern where a template parameter is used as a base class

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// Type aliases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Template with variadic parameters
template<typename... Ts>
struct my_or;

// Specialization: no arguments = false
template<>
struct my_or<> : false_type {};

// Specialization: template parameter as base class
// This is the key test - T is a template parameter used as a base class
template<typename T>
struct my_or<T> : T {};

// NOTE: The following variadic recursive pattern would require complex boolean
// expressions in template arguments, which is a separate parser limitation:
// template<typename T, typename... Rest>
// struct my_or<T, Rest...> : integral_constant<bool, T::value || my_or<Rest...>::value> {};

int main() {
    // Test that the templates compile and instantiate correctly
    // The core feature (template parameter as base class) works!
    
    // Instantiate the templates
    my_or<> m1;           // Uses false_type as base
    my_or<true_type> m2;  // Uses true_type (template parameter) as base
    my_or<false_type> m3; // Uses false_type (template parameter) as base
    
    // Return 42 to indicate successful compilation
    return 42;
}
