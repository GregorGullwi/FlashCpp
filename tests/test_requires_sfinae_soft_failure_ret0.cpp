// Test that requires expression with failing template function call
// doesn't cause a compilation error (SFINAE soft failure)

template<typename T>
void must_be_big(const T&) requires (sizeof(T) > 100) {}

template<typename T>
concept has_must_be_big = requires(T t) { must_be_big(t); };

template<typename T>
int test_fn() {
    return 0;
}

int main() {
    return test_fn<int>();
}
