// Out-of-line template member function definitions
// Tests defining template member functions outside the class body

template<typename T>
class Container {
public:
    T add(T a, T b);
    T multiply(T a, T b);
};

// Out-of-line definition of add()
template<typename T>
T Container<T>::add(T a, T b) {
    return a + b;
}

// Out-of-line definition of multiply()
template<typename T>
T Container<T>::multiply(T a, T b) {
    return a * b;
}

int main() {
    Container<int> c;
    int sum = c.add(10, 32);
    int product = c.multiply(sum, 2);
    return product - 42;  // Should return 42
}

