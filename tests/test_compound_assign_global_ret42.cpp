// Test: compound assignment operators on static local and global variables
// Verifies that +=, -=, *=, etc. correctly load, compute, and store back
// to static local variables (which are implemented as globals internally).

int g_val = 0;

void addToGlobal(int x) {
    g_val += x;
}

int counter() {
    static int n = 0;
    n += 7;
    return n;
}

int main() {
    // Test static local compound assignment
    counter();
    counter();
    counter();
    counter();
    counter();
    counter();
    int c = counter();  // 7*7 = 49 - wait, 7*6 = 42
    if (c != 49) return 1;

    // Test global compound assignment
    addToGlobal(21);
    addToGlobal(21);
    if (g_val != 42) return 2;

    // Combined: 49 - 7 = 42
    return c - 7;
}
