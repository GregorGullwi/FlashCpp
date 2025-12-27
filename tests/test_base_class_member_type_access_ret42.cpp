// Test that the parser accepts base class member type access (e.g., Template<T>::type)
// This pattern is used extensively in <type_traits>
// This test just verifies parsing - the semantic handling may still need work

// The fix allows this to parse without errors
template<typename T>
struct wrapper {
    using type = int;
};

template<typename T>
struct negation : wrapper<T>::type {
};

// Simple functional test that doesn't rely on constructors
int main() {
    return 42;
}
