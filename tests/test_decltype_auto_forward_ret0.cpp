// Phase 5 Task 4 regression: decltype(auto) is now a distinct Type::DeclTypeAuto
// instead of reusing plain Type::Auto.  This test verifies that decltype(auto)
// return type and variable declaration compile and execute correctly.
int getAnswer() { return 42; }

decltype(auto) forwardAnswer() { return getAnswer(); }

int main() {
    decltype(auto) x = forwardAnswer();
    return x - 42;  // Expected: 0
}
