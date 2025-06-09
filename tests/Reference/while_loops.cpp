int test_basic_while_loop() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        sum += i;
        i++;
    }
    return sum; // Should be 45 (0+1+2+...+9)
}

int test_while_loop_with_break() {
    int sum = 0;
    int i = 0;
    while (i < 100) {
        if (i >= 5) {
            break;
        }
        sum += i;
        i++;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_while_loop_with_continue() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        i++;
        if (i % 2 == 0) {
            continue; // Skip even numbers
        }
        sum += i;
    }
    return sum; // Should be 25 (1+3+5+7+9)
}

int test_nested_while_loops() {
    int sum = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 3) {
            sum += i * j;
            j++;
        }
        i++;
    }
    return sum; // Should be 8 (0*0+0*1+0*2+1*0+1*1+1*2+2*0+2*1+2*2 = 0+0+0+0+1+2+0+2+4)
}

int test_while_false() {
    int sum = 0;
    while (false) {
        sum = 100; // Should never execute
    }
    return sum; // Should be 0
}

int test_while_true_with_break() {
    int sum = 0;
    int i = 0;
    while (true) {
        if (i >= 3) {
            break;
        }
        sum += i;
        i++;
    }
    return sum; // Should be 3 (0+1+2)
}

int main() {
    int result1 = test_basic_while_loop();
    int result2 = test_while_loop_with_break();
    int result3 = test_while_loop_with_continue();
    int result4 = test_nested_while_loops();
    int result5 = test_while_false();
    int result6 = test_while_true_with_break();
    return result1 + result2 + result3 + result4 + result5 + result6;
}
