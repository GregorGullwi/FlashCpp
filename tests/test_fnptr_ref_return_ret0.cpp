// Test: function pointer parameters with reference return types parse correctly
// This tests that the parser can handle reference-returning function pointer declarations
// Bug: Parser failed on patterns like: T& (*fn)(T&) in both free functions and member functions

// Free function taking a function pointer with reference return type
int call(int& (*func)(int&), int& x) {
    return 0;
}

// Struct with member function taking reference-returning function pointer
struct Stream {
    int state;

    int apply(int (*__pf)(int)) {
        return __pf(state);
    }
};

int main() {
    return 0;
}
