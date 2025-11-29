// Test perfect forwarding with variadic templates
// Tests that Args&&... parameter packs work correctly
//
// NOTE: This test avoids printf with parameters due to a known codegen bug
// where function parameters get corrupted when forwarded through multiple functions.
// See PRINTF_PARAMETER_BUG.md for details.

// Simple add function - returns value instead of printing
int add_three(int a, int b, int c) {
    return a + b + c;
}

// Perfect forwarding template - accepts any number of forwarding references
template<typename... Args>
int forward_to_add(Args&&... args) {
    // Pack expansion: args... becomes args_0, args_1, args_2
    return add_three(args...);
}

int main() {
    // Test 1: Simple case
    int result1 = forward_to_add(1, 2, 3);
    
    // Test 2: Different values
    int result2 = forward_to_add(10, 20, 30);
    
    // Return 0 if both results are correct
    // result1 should be 6 (1+2+3)
    // result2 should be 60 (10+20+30)
    return (result1 == 6 && result2 == 60) ? 0 : 1;
}
