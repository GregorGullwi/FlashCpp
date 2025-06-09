int test_basic_assignment() {
    int a = 10;
    a = 20;
    return a;
}

int test_compound_assignments() {
    int a = 10;
    a += 5;   // a = 15
    a -= 3;   // a = 12
    a *= 2;   // a = 24
    a /= 4;   // a = 6
    a %= 4;   // a = 2
    return a;
}

int test_bitwise_assignments() {
    int a = 15;  // 1111 in binary
    a &= 7;      // a = 7 (0111)
    a |= 8;      // a = 15 (1111)
    a ^= 3;      // a = 12 (1100)
    return a;
}

int test_shift_assignments() {
    int a = 4;
    a <<= 2;     // a = 16
    a >>= 1;     // a = 8
    return a;
}

int main() {
    return test_basic_assignment() + test_compound_assignments() + test_bitwise_assignments() + test_shift_assignments();
}
