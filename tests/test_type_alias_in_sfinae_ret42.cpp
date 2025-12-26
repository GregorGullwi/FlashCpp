// Test: Type alias used in expression context in template argument
// This is the failing pattern from <type_traits>

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// enable_if - takes a bool value, not a type
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// This is where the issue occurs: using a type alias NAME in an expression context
// The parser needs to understand that "false_type" here refers to a type whose ::value should be accessed
template<typename T>
struct test {
    // This line tries to use false_type in an expression context (as part of !false_type::value)
    // But in simplified form, if we just pass "false_type" as a template argument expression...
    // Actually, let's try something similar to what might be in the template arguments
    
    using type = typename enable_if<false_type::value, int>::type;
};

int main() {
    // This won't instantiate test<int> because enable_if<false, int> doesn't have ::type
    // So just return 42
    return 42;
}
