// Test for qualified base class name resolution
// Pattern: ns::template<Args>::type

namespace detail {
    template<typename T>
    struct select_base {
        struct type {
            static constexpr int value = 42;
        };
    };
}

// Test case: Using qualified template member type as base class
template<typename T>
struct wrapper : detail::select_base<T>::type {
};

int main() {
    wrapper<int> w;
    return w.value;  // Should return 42
}
