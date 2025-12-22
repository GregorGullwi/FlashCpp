// Test auto type deduction

int test_auto_int() {
    auto x = 42;
    return x;
}

int test_auto_expression() {
    auto x = 10 + 20;
    return x;
}

int test_auto_from_variable() {
    int y = 100;
    auto x = y;
    return x;
}


int main() {
    return test_auto_int();
}
