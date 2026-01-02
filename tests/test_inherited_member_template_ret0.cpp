// Test: Inherited member template function lookup
// This pattern is used extensively in <type_traits> for SFINAE detection

struct false_type {
    static constexpr bool value = false;
};

struct true_type {
    static constexpr bool value = true;
};

struct __do_is_test_impl {
    template<typename _Tp>
    static true_type __test(int);

    template<typename>
    static false_type __test(...);
};

template<typename _Tp>
struct __is_test_impl : public __do_is_test_impl {
    using type = decltype(__test<_Tp>(0));
};

int main() {
    // Should resolve to true_type (from __test<int>(int))
    using result = __is_test_impl<int>::type;
    return result::value ? 0 : 42;
}
