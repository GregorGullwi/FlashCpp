// Test pack expansion in decltype base classes

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using type = integral_constant;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Helper for pack expansion test
namespace detail {
    template<typename... Bn>
    true_type or_fn(int);
    
    template<typename... Bn>
    false_type or_fn(...);
}

// Test pack expansion in decltype base class
template<typename... Bn>
struct logical_or : decltype(detail::or_fn<Bn...>(0)) { };

int main() {
    // Test it compiles
    logical_or<true_type, false_type> test;
    return 42;
}
