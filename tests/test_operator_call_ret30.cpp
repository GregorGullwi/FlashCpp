// Test operator() - call operator (functor)
// The int and double overloads must return distinct values so the test verifies
// that the correct overload is selected by argument type, not just arity.
// int overload:    adder(5)   → 5        (returns x directly)
// double overload: adder(5.0) → 25       (returns x*5 as int)
// Combined:        5 + 25     = 30  ✓

struct Adder {
    int operator()(int x) {
        return x;                        // int path: 5 → 5
    }

    int operator()(double x) {
        return static_cast<int>(x) * 5; // double path: 5.0 → 25
    }
};

int test_functor() {
    Adder adder;
    return adder(5) + adder(5.0);  // Should return 5 + 25 = 30
}


int main() {
    return test_functor();
}
