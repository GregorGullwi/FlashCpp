int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n-1) + fibonacci(n-2);
}

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n-1);
}

int main() {
    int result = 0;
    for (int i = 0; i < 10; i++) {
        result += fibonacci(i) + factorial(i);
    }
    return result;
}
