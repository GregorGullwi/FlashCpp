int test_all_operators(int a, int b) {
    int arithmetic = a + b - a * b / 2;
    int shifts = (a << 1) + (b >> 1);
    int bitwise = (a & b) | (a ^ b);
    return arithmetic + shifts + bitwise;
}

int main() {
    return test_all_operators(10, 5);
}
