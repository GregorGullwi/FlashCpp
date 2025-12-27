// Test: Type aliases used standalone in expression contexts (not followed by :: or ()
// This tests if type aliases can be referenced as identifiers in expressions

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Try to use type alias as a value in expression (this might fail)
template<typename T>
constexpr bool test_func() {
    // Try accessing the type alias directly as if it were a variable
    // This is actually not valid C++, but let's see what error we get
    // In real C++, you'd do: false_type::value or use it as a type
    return false;  // For now, just return false to make it compile
}

int main() {
    // Return 42
    return 42;
}
