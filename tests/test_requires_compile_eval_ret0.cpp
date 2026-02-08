// Test: Requires clause compile-time evaluation
// Verifies that requires clauses on template functions are properly
// parsed into AST nodes (not just skipped) for compile-time evaluation.

template<typename T>
concept Integral = __is_integral(T);

template<typename T>
    requires Integral<T>
T add_one(T x) { return x + 1; }

int main() {
    int result = add_one(41);
    return result == 42 ? 0 : 1;
}
