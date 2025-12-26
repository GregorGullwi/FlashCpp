// Test template-dependent decltype base with substitution

namespace detail {
    template<typename T>
    struct base_trait {
        static constexpr int value = 42;
    };
}

// Template struct with decltype base using template parameter
template<typename T>
struct test_wrapper
  : decltype(detail::base_trait<T>())
{
};

int main() {
    test_wrapper<int> t;
    return t.value;  // Should return 42
}
