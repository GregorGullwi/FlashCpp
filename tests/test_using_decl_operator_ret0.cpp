// Test: using Base::operator Type; and using Base::operator=;
// Using-declarations for conversion operators and assignment operators
// must be parsed correctly inside class/struct bodies.
template<typename T>
struct Base {
    operator T() const { return T{}; }
    Base& operator=(const Base&) = default;
};

template<typename T>
struct Derived : Base<T> {
    using Base<T>::operator T;
    using Base<T>::operator=;
};

int main() {
    return 0;
}
