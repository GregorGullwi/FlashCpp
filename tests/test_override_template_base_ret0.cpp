// Regression test: override check with template base classes
// The parser must accept 'override' annotation when base class is a template
// without generating a compiler error about missing vtable entry.

template<typename T>
struct Base {
    virtual int getValue() { return 10; }
};

struct Derived : Base<int> {
    int getValue() override { return 42; }
};

int main() {
    Derived d;
    return d.getValue() - 42;  // 0
}
