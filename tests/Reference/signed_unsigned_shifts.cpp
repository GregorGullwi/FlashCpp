int signed_shift(int a) {
    return a >> 2;  // signed right shift (arithmetic)
}

unsigned int unsigned_shift(unsigned int a) {
    return a >> 2;  // unsigned right shift (logical)
}

int main() {
    return signed_shift(-8) + unsigned_shift(8u);
}
