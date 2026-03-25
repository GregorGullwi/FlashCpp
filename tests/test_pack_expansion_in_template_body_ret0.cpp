// Test that pack expansion in template member function bodies works correctly
// Regression test: foo(args...) inside template class member function should compile
// and produce correct runtime results.

template<typename... Args>
struct Counter {
    int size() {
        return static_cast<int>(sizeof...(Args));
    }
};

template<typename... Args>
void foo(Args... args) {
    (void)sizeof...(args);
}

template<typename... Args>
struct Bar {
    void test(Args... args) {
        foo(args...);
    }
};

int main() {
    Counter<int, int, int> c3;
    Counter<int> c1;
    Counter<int, int> c2;
    if (c3.size() != 3) return 1;
    if (c1.size() != 1) return 2;
    if (c2.size() != 2) return 3;

    Bar<int, int> b;
    b.test(1, 2);

    Bar<int> b2;
    b2.test(42);

    return 0;
}
