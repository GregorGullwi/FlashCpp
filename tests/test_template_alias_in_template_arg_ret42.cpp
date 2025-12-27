// Test: Template aliases in expression contexts
// This is the specific issue from <type_traits>

// Simple template
template<typename T>
struct remove_const {
    using type = T;
};

// Template alias (C++11 feature)
template<typename T>
using remove_const_t = typename remove_const<T>::type;

// Now try to use the template alias in a template argument
template<typename T>
struct helper {
    static constexpr int value = 1;
};

template<typename T>
struct test_struct {
    // This should work: using template alias as a template argument
    static constexpr int result = helper<remove_const_t<T>>::value;
};

int main() {
    return test_struct<int>::result + 41;  // Should return 42 (1 + 41)
}
