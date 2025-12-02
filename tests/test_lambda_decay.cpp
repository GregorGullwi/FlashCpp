// Test lambda decay to function pointer

int main() {
    // Test 1: Lambda decay with unary +
    int (*fp)() = +[]() { return 42; };
    
    // Test 2: Call the function pointer
    int result = fp();
    
    // Test 3: Lambda with parameters
    int (*fp2)(int, int) = +[](int a, int b) { return a + b; };
    int sum = fp2(10, 20);
    
    // Return 0 if all tests pass
    int errors = 0;
    if (result != 42) errors += 1;
    if (sum != 30) errors += 2;
    return errors;
}
