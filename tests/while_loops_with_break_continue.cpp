// While loops test using only implemented features
// Uses: break, continue, ++, --, +, -, *, /, <, >, <=, >=, ==, !=, &&, ||, =

int test_basic_while_loop() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        sum = sum + i;
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
        sum = sum + i;
        i++;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_while_loop_with_continue() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        i++;
        // Skip i==2 and i==4
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            continue;
        }
        sum = sum + i;
    }
    return sum; // Should be 49 (1+3+5+6+7+8+9+10)
}

int test_nested_while_loops() {
    int sum = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 3) {
            sum = sum + (i * j);
            j++;
        }
        i++;
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

int test_while_one_with_break() {
    int sum = 0;
    int i = 0;
    while (1) {  // Using 1 instead of true
        if (i >= 3) {
            break;
        }
        sum = sum + i;
        i++;
    }
    return sum; // Should be 3 (0+1+2)
}

int test_for_loop_with_break() {
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        if (i >= 5) {
            break;
        }
        sum = sum + i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop_with_continue() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            continue;
        }
        sum = sum + i;
    }
    return sum; // Should be 43 (0+1+3+5+6+7+8+9)
}

int test_do_while_with_break() {
    int sum = 0;
    int i = 0;
    do {
        if (i >= 3) {
            break;
        }
        sum = sum + i;
        i++;
    } while (i < 100);
    return sum; // Should be 3 (0+1+2)
}

int test_do_while_with_continue() {
    int sum = 0;
    int i = 0;
    do {
        i++;
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            continue;
        }
        sum = sum + i;
    } while (i < 10);
    return sum; // Should be 49 (1+3+5+6+7+8+9+10)
}

int main() {
    int result1 = test_basic_while_loop();
    int result2 = test_while_loop_with_break();
    int result3 = test_while_loop_with_continue();
    int result4 = test_nested_while_loops();
    int result5 = test_while_zero();
    int result6 = test_while_one_with_break();
    int result7 = test_for_loop_with_break();
    int result8 = test_for_loop_with_continue();
    int result9 = test_do_while_with_break();
    int result10 = test_do_while_with_continue();
    
    return result1 + result2 + result3 + result4 + result5 + result6 + result7 + result8 + result9 + result10;
    // 45 + 10 + 49 + 8 + 0 + 3 + 10 + 43 + 3 + 49 = 220
}

