// Phase 5 Task 1 regression: semantic-pass second-pass auto return deduction.
// Functions whose return type was still Type::Auto after parser-time deduction
// (e.g. friend functions inside a struct) are resolved in the sema pass.
struct Wrapper {
    int val;
    friend auto extractVal(Wrapper w) { return w.val; }
};

// Plain auto return: parser deduces this during finalization
auto doubleIt(int x) { return x * 2; }

int main() {
    Wrapper w{21};
    return extractVal(w) + doubleIt(0) - 21;  // Expected: 0
}
