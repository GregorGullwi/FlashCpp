// Test: Member template alias parsing in template specializations
// This test verifies that member template aliases are correctly parsed
// in both full and partial template specializations.

template<bool B>
struct Conditional {
    template<typename T, typename U>
    using type = T;
};

// Full specialization with member template alias
template<>
struct Conditional<false> {
    template<typename T, typename U>
    using type = U;
};

// Another test with partial specialization
template<typename T, int N>
struct Array {
    template<typename U>
    using pointer = U*;
};

// Partial specialization with member template alias
template<typename T>
struct Array<T, 0> {
    template<typename U>
    using pointer = U**;
};

int main() {
    // Just return success - we only care that it parses
    return 0;
}
