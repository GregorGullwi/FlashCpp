// Test __builtin_constant_p intrinsic
// Expected return: 42
// Tests that constant expressions are detected correctly

int main() {
    int x = 10;
    
    // Test 1: Literal constant - should return 1
    int const_literal = __builtin_constant_p(42);
    
    // Test 2: Variable - should return 0
    int var_result = __builtin_constant_p(x);
    
    // Test 3: Constant expression (arithmetic) - should return 1
    int const_expr = __builtin_constant_p(10 + 20);
    
    // Test 4: Another literal - should return 1
    int const_zero = __builtin_constant_p(0);
    
    // Sum: 1 + 0 + 1 + 1 = 3, but we want to return 42 for the test
    // So we use an if statement to validate the behavior:
    // If all constant checks work correctly, return 42
    // const_literal should be 1
    // var_result should be 0
    // const_expr should be 1
    // const_zero should be 1
    
    if (const_literal == 1 && var_result == 0 && const_expr == 1 && const_zero == 1) {
        return 42;  // All checks passed
    }
    return 0;  // Something failed
}
