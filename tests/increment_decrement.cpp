int test_prefix_increment() {
    int a = 5;
    int b = ++a;  // a = 6, b = 6
    return a + b;
}

int test_postfix_increment() {
    int a = 5;
    int b = a++;  // a = 6, b = 5
    return a + b;
}

int test_prefix_decrement() {
    int a = 5;
    int b = --a;  // a = 4, b = 4
    return a + b;
}

int test_postfix_decrement() {
    int a = 5;
    int b = a--;  // a = 4, b = 5
    return a + b;
}

int test_mixed_operations() {
    int a = 10;
    int result = ++a + a-- + --a + a++;
    // ++a: a=11, result += 11
    // a--: result += 11, a=10
    // --a: a=9, result += 9
    // a++: result += 9, a=10
    // result = 11 + 11 + 9 + 9 = 40
    return result + a;  // 40 + 10 = 50
}

int main() {
    return test_prefix_increment() + test_postfix_increment() + test_prefix_decrement() + test_postfix_decrement();
}
