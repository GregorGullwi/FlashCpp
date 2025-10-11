int test_while_loop() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        sum = sum + i;
        i = i + 1;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_do_while_loop() {
    int sum = 0;
    int i = 0;
    do {
        sum = sum + i;
        i = i + 1;
    } while (i < 5);
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_for_loop() {
    int sum = 0;
    for (int i = 0; i < 5; i = i + 1) {
        sum = sum + i;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_do_while_executes_once() {
    int count = 0;
    do {
        count = count + 1;
    } while (0);  // False condition, but body executes once
    return count; // Should be 1
}

int main() {
    int result = test_while_loop() + test_do_while_loop() + test_for_loop() + test_do_while_executes_once();
    // 10 + 10 + 10 + 1 = 31
    return result;
}

