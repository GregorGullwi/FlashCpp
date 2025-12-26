// Test for pack expansion in decltype base class specification
// This is the primary blocker pattern from <type_traits>

namespace detail {
    // Base types for selection
    struct true_base {
        static constexpr int value = 42;
    };
    
    struct false_base {
        static constexpr int value = 0;
    };
    
    // Helper function that returns true_base if any argument is true
    // This simulates the __or_fn pattern from <type_traits>
    template<typename... Args>
    true_base or_fn(int) { return {}; }
    
    // Fallback that returns false_base
    template<typename... Args>
    false_base or_fn(...) { return {}; }
}

// Test case: struct with variadic pack in decltype base
// Pattern: decltype(detail::or_fn<Bn...>(0))
template<typename... Bn>
struct logical_or
  : decltype(detail::or_fn<Bn...>(0))
{
};

// Instantiate with concrete types - should inherit from true_base
using test_type = logical_or<int, char, float>;

int main() {
    test_type t;
    return t.value;  // Should return 42 from true_base
}
