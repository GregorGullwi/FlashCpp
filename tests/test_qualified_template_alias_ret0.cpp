// Test for qualified template alias in template argument context
// This tests the fix for patterns like: SomeTemplate<namespace::alias<concrete_type>>
// where 'alias' is a template alias defined in a namespace

namespace detail {
    template<typename T>
    using cref = T;
}

template<typename T>
struct Wrapper {
    static constexpr int value = 42;
};

int main() {
    // Test that qualified template alias is correctly resolved
    // detail::cref<int> should resolve to int, so Wrapper<int>::value should be 42
    return Wrapper<detail::cref<int>>::value == 42 ? 0 : 1;
}
