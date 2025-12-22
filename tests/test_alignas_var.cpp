// Test alignas on variables

int test() {
    // Test 1: local variable with alignas(16)
    alignas(16) int x = 10;

    // Test 2: local variable with alignas(32)
    alignas(32) int y = 20;

    // Test 3: local variable with alignas(8) - natural alignment
    alignas(8) int z = 7;

    // Test 4: variable without alignas
    int w = 3;

    return x + y + z + w;  // Should return 10 + 20 + 7 + 3 = 40
}


int main() {
    return test();
}
