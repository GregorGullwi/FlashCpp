// Test: Type alias used as template argument (the specific failing case from <type_traits>)
// Issue: false_type is a type alias but used as an identifier in template argument expression

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// These are type aliases (like in std::)
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// This simulates a pattern from <type_traits> where type aliases are used as template arguments
// The parser sees "false_type" as an identifier in an expression context (template argument list)
// and needs to resolve it from gTypesByName, not just gSymbolTable
template<typename... Ts>
struct my_or;

// Specialization with type alias as argument
// This is where "false_type" appears as an identifier followed by '>'
template<>
struct my_or<> : false_type {};  // Base case: no arguments = false

template<typename T>
struct my_or<T> : T {};  // Single argument: return that type

int main() {
    // Test that we can access the value from the instantiation
    constexpr bool result = my_or<>::value;  // Should be false (0)
    
    // Return 42 if result is false (which it should be)
    return result ? 0 : 42;
}
