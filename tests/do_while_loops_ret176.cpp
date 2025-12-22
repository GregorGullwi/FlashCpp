int test_basic_do_while_loop() {
    int sum = 0;
    int i = 0;
    do {
        sum += i;
        i++;
    } while (i < 10);
    return sum; // Should be 45 (0+1+2+...+9)
}

int test_do_while_loop_with_break() {
    int sum = 0;
    int i = 0;
    do {
        if (i >= 5) {
            break;
        }
        sum += i;
        i++;
    } while (i < 100);
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_do_while_loop_with_continue() {
    int sum = 0;
    int i = 0;
    do {
        i++;
        if (i % 2 == 0) {
            continue; // Skip even numbers
        }
        sum += i;
    } while (i < 10);
    return sum; // Should be 25 (1+3+5+7+9)
}

int test_do_while_false() {
    int sum = 0;
    int i = 0;
    do {
        sum += i;
        i++;
    } while (false);
    return sum; // Should be 0 (executes once with i=0)
}

int test_do_while_true_with_break() {
    int sum = 0;
    int i = 0;
    do {
        if (i >= 3) {
            break;
        }
        sum += i;
        i++;
    } while (true);
    return sum; // Should be 3 (0+1+2)
}

int test_nested_do_while_loops() {
    int sum = 0;
    int i = 0;
    do {
        int j = 0;
        do {
            sum += i * j;
            j++;
        } while (j < 3);
        i++;
    } while (i < 3);
    return sum; // Should be 8 (0*0+0*1+0*2+1*0+1*1+1*2+2*0+2*1+2*2 = 0+0+0+0+1+2+0+2+4)
}

int test_do_while_single_iteration() {
    int value = 42;
    do {
        value *= 2;
    } while (value < 0); // Condition is false, but executes once
    return value; // Should be 84
}

int main() {
    int result1 = test_basic_do_while_loop();
    int result2 = test_do_while_loop_with_break();
    int result3 = test_do_while_loop_with_continue();
    int result4 = test_do_while_false();
    int result5 = test_do_while_true_with_break();
    int result6 = test_nested_do_while_loops();
    int result7 = test_do_while_single_iteration();
    return result1 + result2 + result3 + result4 + result5 + result6 + result7;
}
