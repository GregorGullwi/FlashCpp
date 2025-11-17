// Test basic function pointer member

int add(int a, int b) {
    return a + b;
}

struct Calc {
    int (*operation)(int, int);
};

int main() {
    Calc c;
    c.operation = add;
    return c.operation(5, 3) - 8;  // Should return 0
}
