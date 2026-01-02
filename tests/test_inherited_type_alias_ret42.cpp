// Test inherited type alias lookup
// This tests the feature: wrapper<T>::type where type comes from base class

template<bool B>
struct bool_constant {
    static constexpr bool value = B;
    using type = bool_constant;
};

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

// wrapper inherits from T, so wrapper<true_type>::type should be bool_constant<true>
template<typename T>
struct wrapper : T {};

// Use wrapper<true_type>::type as base class
struct test : wrapper<true_type>::type {
};

int main() {
    return test::value ? 42 : 0;
}
