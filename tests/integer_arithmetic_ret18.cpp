// Combined integer arithmetic tests
int add(int a, int b) {
    return a + b;
}

int subtract(int a, int b) {
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

int test_all_operators(int a, int b) {
    int arithmetic = a + b - a * b / 2;
    int shifts = (a << 1) + (b >> 1);
    int bitwise = (a & b) | (a ^ b);
    return arithmetic + shifts + bitwise;
}

int main() {
	int basic = complex_math(10, 5, 20, 8);  // 6
	int comprehensive = test_all_operators(3, 2);  // 12
	return basic + comprehensive;  // 18
}
