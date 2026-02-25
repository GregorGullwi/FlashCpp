struct Calculator {
    static int add(int a, int b) {
        return a + b;
    }
    
    static int multiply(int a, int b) {
        return a * b;
    }
};

int main() {
    int sum = Calculator::add(10, 20);
    int prod = Calculator::multiply(3, 4);
    return (sum == 30 && prod == 12) ? 42 : 0;
}
