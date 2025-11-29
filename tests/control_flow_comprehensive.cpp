// Test combining if-statements with loops
int test_if_in_for_loop() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) {
            sum += i;
        } else {
            sum += i * 2;
        }
    }
    return sum; // Even: 0+2+4+6+8=20, Odd: 2+6+10+14+18=50, Total: 70
}

int test_for_in_if_statement() {
    int condition = 1;
    int sum = 0;
    if (condition > 0) {
        for (int i = 0; i < 5; i++) {
            sum += i;
        }
    } else {
        for (int i = 0; i < 3; i++) {
            sum += i * 2;
        }
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_while_with_if_break() {
    int sum = 0;
    int i = 0;
    while (true) {
        if (i >= 5) {
            break;
        }
        if (i % 2 == 1) {
            sum += i;
        }
        i++;
    }
    return sum; // Should be 4 (1+3)
}

int test_do_while_with_nested_if() {
    int sum = 0;
    int i = 0;
    do {
        if (i < 3) {
            if (i > 0) {
                sum += i;
            }
        }
        i++;
    } while (i < 5);
    return sum; // Should be 3 (1+2)
}

// Test C++20 if with initializer (when implemented)
int test_if_with_init_concept() {
    int sum = 0;
    // This would be: if (int x = 5; x > 0)
    // For now, simulate with regular if
    int x = 5;
    if (x > 0) {
        sum = x * 2;
    }
    return sum; // Should be 10
}

int test_complex_nested_control_flow() {
    int result = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 0) {
            int j = 0;
            while (j < 2) {
                result += j;
                j++;
            }
        } else if (i == 1) {
            int k = 0;
            do {
                result += k * 2;
                k++;
            } while (k < 2);
        } else {
            for (int m = 0; m < 2; m++) {
                if (m == 1) {
                    result += 5;
                }
            }
        }
    }
    return result; // i=0: 0+1=1, i=1: 0+2=2, i=2: 5, Total: 8
}

int test_break_continue_combinations() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i < 3) {
            continue;
        }
        if (i > 7) {
            break;
        }
        sum += i;
    }
    return sum; // Should be 22 (3+4+5+6+7)
}

int main() {
    int result1 = test_if_in_for_loop();
    int result2 = test_for_in_if_statement();
    int result3 = test_while_with_if_break();
    int result4 = test_do_while_with_nested_if();
    int result5 = test_if_with_init_concept();
    int result6 = test_complex_nested_control_flow();
    int result7 = test_break_continue_combinations();
    return result1 + result2 + result3 + result4 + result5 + result6 + result7;
}
