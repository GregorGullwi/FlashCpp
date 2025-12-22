int test_basic_for_loop() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += i;
    }
    return sum; // Should be 45 (0+1+2+...+9)
}

int test_for_loop_with_break() {
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        if (i >= 5) {
            break;
        }
        sum += i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop_with_continue() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) {
            continue; // Skip even numbers
        }
        sum += i;
    }
    return sum; // Should be 25 (1+3+5+7+9)
}

int test_nested_for_loops() {
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            sum += i * j;
        }
    }
    return sum; // Should be 8 (0*0+0*1+0*2+1*0+1*1+1*2+2*0+2*1+2*2 = 0+0+0+0+1+2+0+2+4)
}

int test_for_loop_no_init() {
    int i = 0;
    int sum = 0;
    for (; i < 5; i++) {
        sum += i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop_no_condition() {
    int sum = 0;
    for (int i = 0; ; i++) {
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
        i++;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_empty_for_loop() {
    int count = 0;
    for (;;) {
        count++;
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
    int result4 = test_nested_for_loops();
    return result1 + result2 + result3 + result4;
}
