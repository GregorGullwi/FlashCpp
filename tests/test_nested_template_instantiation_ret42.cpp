// Test nested template instantiation: member function template of a class template
// Both inline and out-of-line definitions should work

template<typename T>
struct Container {
    // Inline member function template
    template<typename U>
    T convert_inline(U u) { return u; }

    // Out-of-line member function template (declaration only)
    template<typename U>
    T convert_ool(U u);
};

// Out-of-line definition of nested template
template<typename T>
template<typename U>
T Container<T>::convert_ool(U u) {
    return u;
}

int main() {
    Container<int> c;

    // Test inline version
    int r1 = c.convert_inline<double>(21.7);  // double 21.7 -> int 21

    // Test out-of-line version
    int r2 = c.convert_ool<double>(21.3);  // double 21.3 -> int 21

    return r1 + r2;  // 21 + 21 = 42
}
