// Test float comparison operations

int test_feq() {
    double a = 10.5;
    double b = 10.5;
    return (a == b) ? 1 : 0;  // Should be 1
}

int test_fne() {
    double a = 10.5;
    double b = 5.5;
    return (a != b) ? 1 : 0;  // Should be 1
}

int test_flt() {
    double a = 5.5;
    double b = 10.5;
    return (a < b) ? 1 : 0;  // Should be 1
}

int test_fle() {
    double a = 10.5;
    double b = 10.5;
    return (a <= b) ? 1 : 0;  // Should be 1
}

int test_fgt() {
    double a = 15.5;
    double b = 10.5;
    return (a > b) ? 1 : 0;  // Should be 1
}

int test_fge() {
    double a = 10.5;
    double b = 10.5;
    return (a >= b) ? 1 : 0;  // Should be 1
}

int main() {
    int r1 = test_feq();  // 1
    int r2 = test_fne();  // 1
    int r3 = test_flt();  // 1
    int r4 = test_fle();  // 1
    int r5 = test_fgt();  // 1
    int r6 = test_fge();  // 1
    // Total: 1+1+1+1+1+1 = 6
    return r1 + r2 + r3 + r4 + r5 + r6;
}
