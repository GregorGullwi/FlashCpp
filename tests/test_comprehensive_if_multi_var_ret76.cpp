// Test comprehensive if-statements with comma-separated variable declarations

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

int test_multi_var() {
    int a = 5, b = 3, c = 8;
    if (a > b) {
        if (c > a) {
            return a + b + c;
        } else {
            return a + b;
        }
    } else {
        return 0;
    }
}

int test_multi_var_no_init() {
    int x, y, z;
    x = 10;
    y = 20;
    z = 30;
    return x + y + z;
}

int main() {
    int result1 = test_simple_if(5);
    int result2 = test_if_else(-3);
    int result3 = test_multi_var();
    int result4 = test_multi_var_no_init();
    return result1 + result2 + result3 + result4;
}

