// Test that pack expansion in template member function bodies works correctly
// Regression test: foo(args...) inside template class member function should compile

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

template<typename T>
void forward_call(T val) {
    foo(val);
}

int main() {
    Bar<int, double> b;
    b.test(1, 2.0);
    
    Bar<int> b2;
    b2.test(42);
    
    return 0;
}
