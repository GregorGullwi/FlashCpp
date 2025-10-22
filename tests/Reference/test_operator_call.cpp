// Test operator() - call operator (functor)

struct Adder {
    int value;

    int operator()(int x) {
        return value + x;
    }
};

int test_functor() {
    Adder add10;
    add10.value = 10;
    return add10(5);  // Should return 15
}

