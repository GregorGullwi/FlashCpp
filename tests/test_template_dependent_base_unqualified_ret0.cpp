template<typename T>
struct Base {
    int value = 0;
};

template<typename T>
struct Derived : Base<T> {
    int f() { return this->value; }
};

int main() {
    Derived<int> d;
    return d.f();
}
