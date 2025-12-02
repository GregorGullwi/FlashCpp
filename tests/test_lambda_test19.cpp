// Test 19: Lambda arithmetic
int test_lambda_arithmetic() {
    int a = 5;
    int b = 0;
    int temp = a + b; // 5
    auto lambda = [temp]() {
        return temp; 
    };
    return lambda();  // Expected: 5
}

int main() {
    return test_lambda_arithmetic();
}
