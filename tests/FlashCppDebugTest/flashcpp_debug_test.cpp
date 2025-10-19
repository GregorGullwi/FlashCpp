int add(int a, int b) {
    int c = a + b;
    return c;
}

int main() {
    return add(3, 5);  // Should compute: (10 + 5) * (20 - 8) / (10 + 20) = 6
}

/*int subtract(int a, int b) {
    return a - b;
}

int multiply(int a, int b) {
    return a * b;
}

int divide(int a, int b) {
    return a / b;
}

int complex_math(int a, int b, int c, int d) {
    // This will test nested function calls and all arithmetic operations
    // (a + b) * (c - d) / (a + c)
    return divide(
        multiply(
            add(a, b),
            subtract(c, d)
        ),
        add(a, c)
    );
}

int main() {
    return complex_math(10, 5, 20, 8);  // Should compute: (10 + 5) * (20 - 8) / (10 + 20) = 6
}*/