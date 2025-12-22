// Test: Member function references later member variable
// C++20 Rule: Inline member function bodies are parsed in complete-class context

struct MyClass {
    int getValue() { return value; }  // References 'value' declared later
    int value = 42;
};

int main() {
    MyClass obj;
    return obj.getValue();
}

