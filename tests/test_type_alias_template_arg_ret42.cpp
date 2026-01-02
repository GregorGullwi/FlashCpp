// Test type alias used as template argument
// This tests the fix for type alias lookup in template argument context

template<typename T>
struct wrapper {
    using type = T;
};

template<typename T, typename... Args>
using first_t = T;

// Type aliases similar to std::true_type/false_type
template<bool B>
struct bool_constant {
    static constexpr bool value = B;
};

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

// Test: type alias (false_type) used as template argument
// This pattern is used extensively in <type_traits>
template<typename... Bn>
auto test_fn(int) -> first_t<false_type>;

int main() {
    // If compilation succeeds, the fix for type alias in template argument context works
    return 42;
}
