// Test __is_constant_evaluated

int test_is_constant_evaluated() {
    int result = 0;
    // At runtime, __is_constant_evaluated() returns false
    if (!__is_constant_evaluated()) result += 1;
    // But we can still test the intrinsic parses
    result += 2;  // Always add this to show the test ran
    
    return result;  // Expected: 3
}

int main() {
    return test_is_constant_evaluated();
}
