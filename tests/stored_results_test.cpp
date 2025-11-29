int test_func(int a) {
    if (a > 0) {
        return 1;
    }
    return 0;
}

int main() {
    int result1 = test_func(5);
    int result2 = test_func(-3);
    return result1 + result2;
}
