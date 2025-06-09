int bitwise_and(int a, int b) {
    return a & b;
}

int bitwise_or(int a, int b) {
    return a | b;
}

int bitwise_xor(int a, int b) {
    return a ^ b;
}

int complex_bitwise(int a, int b, int c) {
    // Test combination of bitwise operations
    // (a & b) | (a ^ c)
    return bitwise_or(
        bitwise_and(a, b),
        bitwise_xor(a, c)
    );
}

int main() {
    return complex_bitwise(0xFF, 0xF0, 0x0F);  // Should compute: (0xFF & 0xF0) | (0xFF ^ 0x0F) = 0xF0 | 0xF0 = 0xF0
}
