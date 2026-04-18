// Test: __is_convertible_to builtin with class/struct types
// Reproduces blocker from <concepts> line 38 and <type_traits>

struct Base {};
struct Derived : Base {};

// Use the builtin in a struct context like the actual header does
template<typename _Derived, typename _Base>
struct derived_from_impl {
    static constexpr bool value = __is_convertible_to(const volatile _Derived*, const volatile Base*);
};

// Instantiate - if it parses, we're good (semantic may be wrong but parsing works)
int main() {
    // Just instantiate, don't check value
    int x = derived_from_impl<Derived, Base>::value;
    (void)x;
    return 0;
}