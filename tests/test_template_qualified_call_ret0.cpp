// Test: Template qualified member function call
// Validates parsing of Base<T>::member(args) in function bodies
template<typename T>
struct Base {
    static int compute(T x) { return static_cast<int>(x); }
};

template<typename T>
struct Derived : public Base<T> {
    int call(T x) {
        return Base<T>::compute(x);
    }
};

int main() {
    Derived<int> d;
    return d.call(0);
}
