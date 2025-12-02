int test_simple() {
    int x = 5;
    auto lambda = [x]() {
        return x;
    };
    return lambda();
}

int main() {
    return test_simple();
}
