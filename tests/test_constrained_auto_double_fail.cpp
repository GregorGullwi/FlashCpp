// Test: C++20 constrained auto parameter rejects non-matching type
// Passing a double to a parameter constrained with IsInt (requires int) must fail

template<typename T>
concept IsInt = __is_same(T, int);

int identity(IsInt auto x) {
    return x;
}

int main() {
    return identity(1.0);  // double should not satisfy IsInt
}
