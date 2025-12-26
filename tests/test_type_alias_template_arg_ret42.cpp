// Test: Type alias as a dependent name in template argument expressions
// This mimics how <type_traits> uses type aliases in template metaprogramming

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Template that expects a type as an argument
template<typename T>
struct identity {
    using type = T;
};

// Use type alias as a template type argument (not as base class)
// This is where the parser sees "false_type" as an identifier in template args
template<typename T = false_type>  // Type alias as default template argument
struct test_struct {
    using type = T;
};

int main() {
    // Access the nested type
    constexpr bool value = test_struct<>::type::value;
    
    // Return 42 if value is false (0)
    return value ? 0 : 42;
}
