// Test that requires expression with failing function call
// results in false constraint (SFINAE) rather than hard error

template<typename T>
void check_fn(const T&) requires (sizeof(T) > 100) {}

template<typename T>
concept has_check = requires(T t) { check_fn(t); };

int main() {
    // int doesn't satisfy sizeof(T) > 100, so has_check<int> should be false
    // This should compile without errors
    static_assert(!has_check<int>, "int should not satisfy has_check");
    return 0;
}
