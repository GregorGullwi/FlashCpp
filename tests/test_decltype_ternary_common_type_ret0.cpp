// Test decltype with ternary expression
// This was blocking <type_traits> at line 2351

template<typename T>
T declval();

template<typename T, typename U>
struct common_type_simple {
    using type = decltype(true ? declval<T>() : declval<U>());
};

int main() {
    // The compile time check is just that this parses and compiles
    return 0;
}
