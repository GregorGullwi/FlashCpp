int test_simple_if(int a) {
    if (a > 0) {
        return 1;
    }
    return 0;
}

int test_if_else(int a) {
    if (a > 0) {
        return 1;
    } else {
        return -1;
    }
}

int test_nested_if(int a, int b) {
    if (a > 0) {
        if (b > 0) {
            return 1;
        } else {
            return 2;
        }
    } else {
        return 0;
    }
}

int test_complex_condition(int a, int b) {
    if (a > 0 && b < 10) {
        return a + b;
    }
    return 0;
}

int main() {
    int result1 = test_simple_if(5);
    int result2 = test_if_else(-3);
    int result3 = test_nested_if(1, 2);
    int result4 = test_complex_condition(3, 7);
    return result1 + result2 + result3 + result4;
}
