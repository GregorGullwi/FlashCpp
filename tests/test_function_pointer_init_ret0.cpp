// Test: Function pointer declaration with initialization
// This tests function pointer initialization

int add(int a, int b) {
    return a + b;
}

int main() {
    // Function pointer declaration with initialization
    int (*fp)(int, int) = add;
    
    return 0;
}

