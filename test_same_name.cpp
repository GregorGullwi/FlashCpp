// Test init-capture with same name
int test_same_name() {
    int x = 2;
    auto lambda = [x = x+3]() { return x; };
    return lambda();
}

int main() {
    return test_same_name();
}
