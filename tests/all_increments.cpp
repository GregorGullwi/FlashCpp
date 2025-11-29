// Test increment and decrement operations

int test_prefix_increment() {
    int a = 5;
    int b = ++a;  // a = 6, b = 6
    return a + b;  // Expected: 12
}

int test_postfix_increment() {
    int a = 5;
    int b = a++;  // b = 5, a = 6
    return a + b;  // Expected: 11
}

int test_prefix_decrement() {
    int a = 5;
    int b = --a;  // a = 4, b = 4
    return a + b;  // Expected: 8
}

int test_postfix_decrement() {
    int a = 5;
    int b = a--;  // b = 5, a = 4
    return a + b;  // Expected: 9
}

int test_increment_in_loop() {
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += i;
    }
    return sum;  // Expected: 0+1+2+3+4 = 10
}

int test_decrement_in_loop() {
    int sum = 0;
    for (int i = 4; i >= 0; i--) {
        sum += i;
    }
    return sum;  // Expected: 4+3+2+1+0 = 10
}

int test_compound_increment() {
    int a = 0;
    a += 5;  // a = 5
    a -= 2;  // a = 3
    a *= 4;  // a = 12
    a /= 3;  // a = 4
    return a;  // Expected: 4
}

int main() {
    int r1 = test_prefix_increment();
    int r2 = test_postfix_increment();
    int r3 = test_prefix_decrement();
    int r4 = test_postfix_decrement();
    int r5 = test_increment_in_loop();
    int r6 = test_decrement_in_loop();
    int r7 = test_compound_increment();
    
    return 0;
}
