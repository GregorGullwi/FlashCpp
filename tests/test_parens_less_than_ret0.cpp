// Test that parenthesized variable expressions followed by < are parsed correctly
// as comparisons, not as C-style casts with template arguments.
// This was previously failing because (x) < 8 was being parsed as if (x) was a C-style cast.

int main() {
    int x = 5;
    
    // Simple case: parenthesized variable followed by less-than comparison
    int y = (x) < 8 ? 10 : 20;
    if (y != 10) return 1;
    
    // Nested ternary with parenthesized expressions
    int z = (x) < 3 ? 100 : ((x) < 6 ? 200 : 300);
    if (z != 200) return 2;
    
    // Test with different variable
    int a = 10;
    int result = (a) < 5 ? 1 : 0;
    if (result != 0) return 3;
    
    // All tests passed
    return 0;
}
