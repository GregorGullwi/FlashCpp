// Comprehensive integration test for type_traits patterns
// Tests both pack expansion and type alias resolution

// Base types
struct true_type {
    static constexpr bool value = true;
};

struct false_type {
    static constexpr bool value = false;
};

// Type aliases
using true_t = true_type;
using false_t = false_type;

// Pack expansion pattern similar to __or_
namespace detail {
    template<typename...>
    true_type or_helper(int);
    
    template<typename...>
    false_type or_helper(...);
}

template<typename... Bn>
struct logical_or : decltype(detail::or_helper<Bn...>(0)) {
};

// Test instantiation
template<typename T, typename U>
struct is_same : false_t {
};

template<typename T>
struct is_same<T, T> : true_t {
};

int main() {
    // Test pack expansion
    logical_or<int, char, float> or_test;
    
    // Test type alias in base class
    is_same<int, int> same_test;
    is_same<int, char> diff_test;
    
    // Should return 42 if true_type::value works
    return same_test.value ? 42 : 0;
}
