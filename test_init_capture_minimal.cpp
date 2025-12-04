// Minimal test for init-capture
int test_init_capture() {
    int base = 3;
    auto lambda = [x = base + 2]() { return x; };
    return lambda();
}

int main() {
    return test_init_capture();
}
