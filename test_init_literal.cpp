// Test init-capture with literal
int test_init_literal() {
    auto lambda = [x = 5]() { return x; };
    return lambda();
}

int main() {
    return test_init_literal();
}
