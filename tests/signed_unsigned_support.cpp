signed int signed_func(signed int a, signed int b) {
    return a / b;
}

unsigned int unsigned_func(unsigned int a, unsigned int b) {
    return a / b;
}

int main() {
    return signed_func(8, 2) + unsigned_func(8u, 2u);
}
