// Test operator() - call operator (functor)

struct Adder {
    int value;

    int operator()(int x) {
        return value + x;
    }

    int operator()(double x) {
        return value + static_cast<int>(x);
    }
};

int test_functor() {
    Adder add10;
    add10.value = 10;
    return add10(5) + add10(5.5);  // Should return 30
}

