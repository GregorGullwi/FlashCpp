// Comprehensive while loop tests using only implemented features
// Uses: +, -, *, /, <, >, <=, >=, ==, !=, &&, ||, =, ++, --

int test_basic_while_loop() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        sum = sum + i;
        ++i;  // Using prefix increment
    }
    return sum; // Should be 45 (0+1+2+...+9)
}

int test_nested_while_loops() {
    int sum = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 3) {
            sum = sum + (i * j);
            ++j;
        }
        ++i;
    }
    return sum; // Should be 8 (0+0+0+0+1+2+0+2+4)
}

int test_while_zero() {
    int sum = 0;
    while (0) {
        sum = 100; // Should never execute
    }
    return sum; // Should be 0
}

int test_while_with_postfix() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        sum = sum + i;
        i++;  // Using postfix increment
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_while_with_decrement() {
    int sum = 0;
    int i = 10;
    while (i > 0) {
        --i;
        sum = sum + i;
    }
    return sum; // Should be 45 (9+8+7+...+1+0)
}

int test_while_complex_condition() {
    int sum = 0;
    int i = 0;
    int j = 10;
    while (i < 5 && j > 5) {
        sum = sum + i + j;
        ++i;
        --j;
    }
    return sum; // Should be 50 (0+10 + 1+9 + 2+8 + 3+7 + 4+6)
}

int main() {
    int result1 = test_basic_while_loop();
    int result2 = test_nested_while_loops();
    int result3 = test_while_zero();
    int result4 = test_while_with_postfix();
    int result5 = test_while_with_decrement();
    int result6 = test_while_complex_condition();
    return result1 + result2 + result3 + result4 + result5 + result6;
    // 45 + 8 + 0 + 10 + 45 + 50 = 158
}

