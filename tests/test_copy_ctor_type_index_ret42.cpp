// Test that copy constructor detection correctly checks type_index
// A constructor Foo(const Bar&) should NOT be treated as a copy constructor for Foo
struct Bar {
    int value;
};

struct Foo {
    int data;
    // This takes a const Bar& — NOT a copy constructor
    Foo(const Bar& b) : data(b.value * 2) {}
    // This IS the copy constructor
    Foo(const Foo& other) : data(other.data) {}
    // Default constructor
    Foo() : data(0) {}
};

int main() {
    Bar b;
    b.value = 21;
    Foo f1(b);   // Uses Foo(const Bar&)
    Foo f2(f1);  // Uses Foo(const Foo&) — actual copy constructor
    return f2.data;  // Should be 42
}
