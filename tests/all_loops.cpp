// Test all loop types: for, while, do-while

int test_for_loop() {
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        sum += i;
    }
    return sum;  // Expected: 0+1+2+3+4+5+6+7+8+9 = 45
}

int test_while_loop() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        sum += i;
        ++i;
    }
    return sum;  // Expected: 0+1+2+3+4 = 10
}

int test_do_while_loop() {
    int sum = 0;
    int i = 0;
    do {
        sum += i;
        ++i;
    } while (i < 3);
    return sum;  // Expected: 0+1+2 = 3
}

int test_nested_loops() {
    int sum = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            sum += i + j;
        }
    }
    return sum;
}

int test_break() {
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        if (i == 5) {
            break;
        }
        sum += i;
    }
    return sum;  // Expected: 0+1+2+3+4 = 10
}

int test_continue() {
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        if (i == 2) {
            continue;
        }
        sum += i;
    }
    return sum;  // Expected: 0+1+3+4 = 8
}

int main() {
    int r1 = test_for_loop();
    int r2 = test_while_loop();
    int r3 = test_do_while_loop();
    int r4 = test_nested_loops();
    int r5 = test_break();
    int r6 = test_continue();
    
    return 0;
}
