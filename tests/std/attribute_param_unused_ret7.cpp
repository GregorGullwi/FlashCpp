// Test: GCC __attribute__ on function parameters
// Tests that __attribute__((__unused__)) on function parameters is properly skipped.
// Expected return: 7

void deallocate(int* p, int n __attribute__ ((__unused__))) {
    // n is marked unused
}

int add(int a __attribute__((__unused__)), int b) {
    return a + b;
}

int main() {
    int x = 3;
    deallocate(&x, 1);
    return add(3, 4);
}
