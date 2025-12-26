// Test for decltype base class specification
// Based on the pattern from <type_traits> line 194

namespace detail {
    template<typename T>
    struct true_type {
        static constexpr bool value = true;
        constexpr operator bool() const { return value; }
    };
    
    template<typename T>
    struct false_type {
        static constexpr bool value = false;
        constexpr operator bool() const { return value; }
    };
    
    // Helper function that returns a type based on condition
    template<bool Cond>
    auto select_type(int) -> true_type<int> { return {}; }
    
    template<bool Cond>
    auto select_type(long) -> false_type<int> { return {}; }
}

// Test case: struct inheriting from decltype expression
// This is the pattern that fails in <type_traits>
template<bool Cond>
struct test_struct
  : decltype(detail::select_type<Cond>(0))
{
};

int main() {
    // Test instantiation
    test_struct<true> t1;
    test_struct<false> t2;
    
    // Access inherited members
    bool b1 = t1.value;  // Should be true
    bool b2 = t2.value;  // Should be false
    
    return b1 ? 42 : 0;
}
