// Test: Member function calls later member function
// C++20 Rule: Inline member function bodies are parsed in complete-class context

struct Test {
    int foo() { return bar(); }  // Calls 'bar' declared later
    int bar() { return 42; }
};

int main() {
    Test t;
    return t.foo();
}

