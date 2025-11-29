// Minimal base constructor test

struct Base {
    int x;
    Base() : x(42) {}
};

struct Derived : Base {
    Derived() : Base() {}
};

int main() {
    Derived d;
    return d.x;  // Should return 42
}
