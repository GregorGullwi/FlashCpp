// Test: GCC __attribute__ between return type and function name
// This pattern is used in libstdc++ atomicity.h
// The compiler must skip __attribute__((...)) when parsing function declarations

int __attribute__((__always_inline__)) add_numbers(int a, int b) {
    return a + b;
}

int __attribute__((__noinline__)) multiply(int a, int b) {
    return a * b;
}

int main() {
    int result = add_numbers(3, 4);
    if (result != 7) return 1;
    result = multiply(2, 5);
    if (result != 10) return 2;
    return 0;
}
