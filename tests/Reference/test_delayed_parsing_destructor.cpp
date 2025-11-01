// Test: Destructor references member variables
// C++20 Rule: Inline member function bodies are parsed in complete-class context

struct Test {
    ~Test() { value = 0; }  // References 'value' declared later
    int value = 42;
};

int main() {
    Test t;
    return 0;
}

