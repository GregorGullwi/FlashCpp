// Test: Template parameter with typename in default value
// This tests parsing of dependent type names as template parameter defaults

template<typename T>
struct wrapper {
    using type = int;
};

// Simple case: typename in template parameter default
template<typename T, typename U = typename wrapper<T>::type>
struct test {
    U value;
};

int main() {
    // Instantiate with default - U should be int (from wrapper<int>::type)
    test<int> t;
    t.value = 42;
    return t.value;  // Should return 42
}
