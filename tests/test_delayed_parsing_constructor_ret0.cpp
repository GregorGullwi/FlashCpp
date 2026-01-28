// Test: Constructor references later member variable
// C++20 Rule: Inline member function bodies are parsed in complete-class context

struct Test {
    Test() : value(getValue()) {}  // Calls getValue() declared later
    int getValue() { return 42; }
    int value;
};

int main() {
    Test t;
    return 0;
}

