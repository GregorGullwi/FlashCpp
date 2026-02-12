// Test bool parameter branching with throw and try/catch
// This tests that bool parameters are correctly loaded (1 byte, not 4 bytes)
// and that CFI entries handle early returns properly for exception unwinding

int conditional_throw(bool b) {
    if (b) {
        return 100;
    }
    throw 1;
}

int main() {
    // Test 1: bool true path - should return 100
    int r1 = conditional_throw(true);
    if (r1 != 100) return 1;

    // Test 2: bool false path with catch - should catch the throw
    int r2 = 0;
    try {
        conditional_throw(false);
        r2 = 99;
    } catch (int e) {
        r2 = e;
    }
    if (r2 != 1) return 2;

    return 0;
}
