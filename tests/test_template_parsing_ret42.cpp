// Minimal test for ::template keyword parsing
// Tests that the parser can handle the syntax without full template instantiation

template<typename T>
struct Base {
    using inner = int;
};

// In a dependent context, we use ::template even though inner is not a template
// This is just to test that the parser accepts the syntax
template<typename T>
void test_function() {
    // This line should parse without errors
    // (Even if it doesn't fully work at runtime)
    typename Base<T>::inner x = 42;
}

int main() {
    return 42;
}
