// Test: static_assert deferral in template struct bodies
// Verifies that static_assert with dependent type traits is deferred
// until template instantiation instead of failing during definition.

template<typename T>
struct HasValue {
    static constexpr bool value = false;
};

template<>
struct HasValue<int> {
    static constexpr bool value = true;
};

template<bool cond, typename T, typename... Args>
struct Checker;

template<typename T, typename... Args>
struct Checker<true, T, Args...> {
    // This static_assert uses dependent types, should be deferred
    static_assert(HasValue<T>::value, "T must have value");
};

template<typename T, typename... Args>
struct Checker<false, T, Args...> {
    // No assertion needed
};

int main() {
    Checker<true, int> c;
    (void)c;
    return 0;
}
