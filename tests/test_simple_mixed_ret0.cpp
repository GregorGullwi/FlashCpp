// Simple test to isolate the issue
float add_two(float f1, double d1) {
    return static_cast<float>(static_cast<double>(f1) + d1);
}

int main() {
    // Test 1: literals
    float r1 = add_two(1.0f, 2.0);
    
    // Test 2: variables
    float fvar = 3.0f;
    double dvar = 4.0;
    float r2 = add_two(fvar, dvar);
    
    // Test 3: from int
    int i = 5;
    float r3 = add_two(static_cast<float>(i), static_cast<double>(i));
    
    return 0;
}
