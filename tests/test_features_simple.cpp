int test_arithmetic() {
    int a = 10;
    int b = 5;
    int sum = a + b;
    return sum;
}

int test_compound() {
    int x = 100;
    x += 10;
    return x;
}

int main() {
    int r1 = test_arithmetic();
    int r2 = test_compound();
    return r1 + r2;
}

