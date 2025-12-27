// Simple test for type alias resolution fix
// Previously failed with: Missing identifier: false_type

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Key test: Using type alias as template type argument
// This is the pattern that was failing in <type_traits>
template<typename T>
struct test {
    using type = T;
};

int main() {
    // Use false_type as a template argument
    // Before the fix, this would fail with "Missing identifier: false_type"
    // when the parser tried to parse template arguments
    constexpr bool value = test<false_type>::type::value;
    
    // Return 42 if value is false (0)
    return value ? 0 : 42;
}
