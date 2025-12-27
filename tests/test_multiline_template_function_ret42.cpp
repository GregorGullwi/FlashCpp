// Test: Multi-line template function declaration with return type spanning multiple lines
// This tests the blocker mentioned in STANDARD_HEADERS_MISSING_FEATURES.md line 131

template<typename T>
struct wrapper {
    using type = int;
};

// Multi-line function declaration - return type on multiple lines
template<typename T>
    typename wrapper<
        T
    >::type 
    test_function(T val) {
    return 42;
}

int main() {
    return test_function(10);  // Should return 42
}
