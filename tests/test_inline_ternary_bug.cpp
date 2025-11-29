// Test inline ternary in return

int main() {
    int a = 5;
    int b = 10;
    bool c = false;
    
    // Should return 15
    return a + b + (c ? 1 : 0);
}
