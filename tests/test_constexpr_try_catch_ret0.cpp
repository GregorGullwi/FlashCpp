// Test: try/catch blocks inside constexpr functions (C++20 [stmt.try])
// A try block inside a constexpr function is allowed by C++20 as long as no
// exception is actually thrown during constant evaluation.  The compiler must
// silently ignore the catch clause at constexpr time.
//
// Expected exit code: 0

constexpr int safe_divide(int a, int b) {
    try {
        if (b == 0) return -1;
        return a / b;
    } catch (...) {
        return -1;
    }
}

int main() {
    constexpr int result = safe_divide(10, 2);
    static_assert(result == 5, "safe_divide(10,2) should be 5");
    return result - 5;  // 0 on success
}
