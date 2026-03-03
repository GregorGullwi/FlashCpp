// Test: function-pointer member called on a temporary expression result
// e.g., getContainer().callback(40, 2) should return 42
// Also tests calling a regular member function on a temporary.

int add(int a, int b) { return a + b; }

struct Container {
    int (*callback)(int, int);

    int invoke(int a, int b) {
        return callback(a, b);
    }
};

Container getContainer() {
    return Container{add};
}

int main() {
    // Test 1: function-pointer member on temporary
    int r1 = getContainer().callback(40, 2);

    // Test 2: regular member function on temporary
    int r2 = getContainer().invoke(20, 1);

    // r1 == 42, r2 == 21 → 42 - 21 = 21 ... need to return 42
    // Just validate both paths produce correct results:
    // r1 + r2 - 21 == 42
    return r1 + r2 - 21;
}
