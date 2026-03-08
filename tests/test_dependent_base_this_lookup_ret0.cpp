template<typename T>
struct Base {
    int value = 0;
    int get() const { return value; }
};

template<typename T>
struct Derived : Base<T> {
    int f() { return this->get(); }
};

int main() {
    Derived<int> d;
    return d.f();
}
