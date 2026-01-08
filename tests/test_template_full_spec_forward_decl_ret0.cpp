// Test forward declaration of full template specialization
// This pattern appears in <type_traits> at line 1917

template<typename T>
struct Container {
    T value;
};

// Forward declaration of full specialization
template<> struct Container<bool>;
template<> struct Container<const bool>;

// Can use the forward-declared type in pointer/reference contexts
void test_func(Container<bool>* ptr) {
    // Just testing that forward declaration allows pointer usage
}

int main() {
    // Don't actually instantiate - just testing that forward declaration parses
    return 0;
}
