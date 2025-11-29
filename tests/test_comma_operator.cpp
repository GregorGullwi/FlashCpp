// Test file for comma operator support in FlashCpp
// The comma operator evaluates left-to-right and returns the rightmost value

int func1() {
    return 10;
}

int func2() {
    return 20;
}

int main() {
    // Test 1: Simple comma operator in assignment
    // Should evaluate func1() (discarded), then assign 2 to a
    int a = (func1(), 2);
    
    // Test 2: Comma operator in return statement
    // Should evaluate func2() (discarded), then return 42
    return (func2(), 42);
}

