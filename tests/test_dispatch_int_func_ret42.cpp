// Test that int-returning function definitions work through unified dispatch
// Expected return: 42

int add(int a, int b) {
    return a + b;
}

static int mul(int a, int b) {
    return a * b;
}

int main() {
    int x = add(20, 1);
    int y = mul(x, 2);
    return y;  // (20 + 1) * 2 = 42
}
