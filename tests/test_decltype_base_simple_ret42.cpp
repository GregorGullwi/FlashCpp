// Simpler test for decltype base class specification
// Without non-type template parameters

namespace detail {
    struct base_a {
        static constexpr int value = 42;
    };
    
    struct base_b {
        static constexpr int value = 99;
    };
    
    // Helper function that returns a type
    base_a select_base(int) { return {}; }
}

// Test case: struct inheriting from decltype expression
// This should inherit from base_a
struct test_struct
  : decltype(detail::select_base(0))
{
};

int main() {
    test_struct t;
    return t.value;  // Should return 42
}
