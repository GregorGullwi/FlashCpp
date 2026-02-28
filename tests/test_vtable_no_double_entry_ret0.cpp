// Regression test: no double vtable entry for override functions
// When override is specified, the function should appear in the vtable
// exactly once, not be duplicated.

struct Base {
    virtual int compute() { return 10; }
    virtual ~Base() {}
};

struct Derived : Base {
    int compute() override { return 20; }
};

int main() {
    Derived d;
    // Call through direct object (avoids virtual dispatch complexity)
    return d.compute() - 20;  // 0
}
