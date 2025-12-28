// Test type alias used in template argument context
// This pattern is common in <type_traits> where type aliases like __remove_cv_t
// are used as template arguments: __is_integral_helper<__remove_cv_t<T>>

template<typename T>
struct remove_const {
    using type = T;
};

// Type alias (not template alias!)
using remove_const_t = typename remove_const<int>::type;

template<typename T>
struct wrapper {
    using type = T;
};

// Use the type alias as a template argument - this is the pattern that fails
template<typename T>
struct test : wrapper<remove_const_t> {
};

int main() {
    test<int> t;
    return 42;
}
