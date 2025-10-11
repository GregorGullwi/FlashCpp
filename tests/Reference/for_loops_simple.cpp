// Simple for loop tests using only supported operators

int test_basic_for_loop() {
    int sum = 0;
    for (int i = 0; i < 5; i = i + 1) {
        sum += i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop_with_break() {
    int sum = 0;
    for (int i = 0; i < 100; i = i + 1) {
        if (i >= 5) {
            break;
        }
        sum += i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop_with_continue() {
    int sum = 0;
    for (int i = 0; i < 10; i = i + 1) {
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            continue;
        }
        sum += i;
    }
    return sum; // Should be 33 (0+1+3+5+6+7+8+9)
}

int test_for_loop_no_init() {
    int i = 0;
    int sum = 0;
    for (; i < 5; i = i + 1) {
        sum += i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop_no_condition() {
    int sum = 0;
    for (int i = 0; ; i = i + 1) {
        if (i >= 3) {
            break;
        }
        sum += i;
    }
    return sum; // Should be 3 (0+1+2)
}

int test_for_loop_no_update() {
    int sum = 0;
    for (int i = 0; i < 5; ) {
        sum += i;
        i = i + 1;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_empty_for_loop() {
    int count = 0;
    for (;;) {
        count = count + 1;
        if (count >= 3) {
            break;
        }
    }
    return count; // Should be 3
}

int main() {
    int result1 = test_basic_for_loop();
    int result2 = test_for_loop_with_break();
    int result3 = test_for_loop_with_continue();
    int result4 = test_for_loop_no_init();
    int result5 = test_for_loop_no_condition();
    int result6 = test_for_loop_no_update();
    int result7 = test_empty_for_loop();
    return result1 + result2 + result3 + result4 + result5 + result6 + result7;
}

