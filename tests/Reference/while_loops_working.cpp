int test_basic_while_loop() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
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
            j = j + 1;
        }
        i = i + 1;
    }
    return sum; // Should be 8 (0*0+0*1+0*2+1*0+1*1+1*2+2*0+2*1+2*2 = 0+0+0+0+1+2+0+2+4)
}

int test_while_zero() {
    int sum = 0;
    while (0) {
        sum = 100; // Should never execute
    }
    return sum; // Should be 0
}

int main() {
    int result1 = test_basic_while_loop();
    int result2 = test_nested_while_loops();
    int result3 = test_while_zero();
    return result1 + result2 + result3; // 45 + 8 + 0 = 53
}

