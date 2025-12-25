// Test auto return type with trailing return type in template function

template<typename T>
struct Helper {
    using type = int;
};

// Template function with auto and trailing return type
template<typename T>
auto test_func(int x) -> typename Helper<T>::type {
    return x + 42;
}

int main() {
    return test_func<double>(0);  // Should return 42
}
