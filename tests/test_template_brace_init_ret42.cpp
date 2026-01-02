// Test template brace initialization (type_identity<T>{} pattern)
// This is a common pattern in <type_traits> header for checking type completeness
template<typename T>
struct type_identity {
    using type = T;
};

int foo(type_identity<int>) {
    return 42;
}

int main() {
    // Template brace initialization creates a temporary object
    int result = foo(type_identity<int>{});
    return result;
}
