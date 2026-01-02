// Test to verify that __is_literal_type triggers a deprecation warning
// This trait is deprecated in C++17 and removed in C++20

int main() {
    // Use __is_literal_type directly to test the warning
    bool test1 = __is_literal_type(int);
    bool test2 = __is_literal_type(double);
    bool test3 = __is_literal_type(char);
    
    return (test1 && test2 && test3) ? 0 : 1;
}
