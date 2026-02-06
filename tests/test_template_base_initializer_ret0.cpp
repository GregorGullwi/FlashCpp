// Test: Template base class in member initializer list
// Validates parsing of Base<T>(args) in constructor initializer lists
template<typename T>
struct Base {
    T value;
    Base(T v) : value(v) {}
};

template<typename T>
struct Derived : public Base<T> {
    Derived(T v) : Base<T>(v) {}
};

int main() {
    Derived<int> d(42);
    return 0;
}
