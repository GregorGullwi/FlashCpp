// Test that requires expression with failing function call
// results in false constraint (SFINAE) rather than hard error

template<typename T>
void check_fn(const T&) requires (sizeof(T) > 100) {}

template<typename T>
concept has_check = requires(T t) { check_fn(t); };

// Also test a concept with a simple type trait that can be satisfied
template<typename T>
concept is_integral_like = requires { T(0); };

int main() {
    // int doesn't satisfy sizeof(T) > 100, so has_check<int> should be false
    // This should compile without errors
    static_assert(!has_check<int>, "int should not satisfy has_check");
    // is_integral_like<int> should be true since int(0) is valid
    static_assert(is_integral_like<int>, "int should satisfy is_integral_like");
    return 0;
}
