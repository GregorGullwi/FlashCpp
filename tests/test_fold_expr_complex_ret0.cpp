// Test fold expression with complex pack expression - parsing only
// Code generation for fold expressions with complex expressions is still WIP
template<typename T>
constexpr bool is_ok() { return true; }

template<typename T>
struct type_identity { using type = T; };

template<typename... Types>
constexpr bool all_ok() {
    return (is_ok<type_identity<Types>>() && ...);
}

// The parsing succeeds, but code generation for fold expressions is still being implemented
int main() {
    // Just verify the template parses correctly
    return 0;
}
