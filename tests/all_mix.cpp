// Test mixed operations combining integers, floats, and doubles
// Tests different arithmetic operations and control flow

int test_int_ops() {
    int a = 10;
    int b = 3;
    int sum = a + b;
    int diff = a - b;
    int prod = a * b;
    int quot = a / b;  // Integer division
    int rem = a % b;   // Modulo
    return sum + diff + prod + quot + rem;
}

float test_float_ops() {
    float x = 5.5f;
    float y = 2.2f;
    float sum = x + y;
    float diff = x - y;
    float prod = x * y;
    float quot = x / y;
    return sum + diff + prod + quot;
}

double test_double_ops() {
    double a = 10.5;
    double b = 2.5;
    double sum = a + b;
    double diff = a - b;
    double prod = a * b;
    double quot = a / b;
    return sum + diff + prod + quot;
}

int test_mixed() {
    int i = 5;
    float f = 3.5f;
    double d = 2.0;
    
    // Mix integer and float
    float result1 = i + f;
    
    // Mix float and double
    double result2 = f + d;
    
    // Mix all three
    double result3 = i + f + d;
    
    return (int)(result1 + result2 + result3);
}

int test_loops() {
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += i;
    }
    
    int j = 0;
    while (j < 3) {
        sum += j;
        ++j;
    }
    
    return sum;
}

int main() {
    int r1 = test_int_ops();
    float r2 = test_float_ops();
    double r3 = test_double_ops();
    int r4 = test_mixed();
    int r5 = test_loops();
    
    return 0;
}
