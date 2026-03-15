// Phase 5 Task 2 regression: generic lambda parameter normalization via sema hook.
// The semantic-pass normalizeGenericLambdaParams() pre-builds resolved declarations
// for auto-typed lambda parameters before body codegen.
int main() {
    auto add = [](auto a, auto b) { return a + b; };
    int result = add(20, 22);
    return result - 42;  // Expected: 0
}
