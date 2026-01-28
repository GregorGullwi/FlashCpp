int signed_shift(int a) {
    return a >> 2;  // signed right shift (arithmetic)
}

unsigned int unsigned_shift(unsigned int a) {
    return a >> 2;  // unsigned right shift (logical)
}

// Expected return: 0 (signed_shift(-8) = -2, unsigned_shift(8u) = 2, -2 + 2 = 0)
int main() {
    return signed_shift(-8) + unsigned_shift(8u);
}
