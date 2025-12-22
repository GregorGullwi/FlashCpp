// Test: Multiple member functions with forward references
// C++20 Rule: Inline member function bodies are parsed in complete-class context

struct Calculator {
    int add() { return x + y; }
    int multiply() { return x * y; }
    int compute() { return add() + multiply(); }
    int x = 10;
    int y = 20;
};

int main() {
    Calculator calc;
    return calc.compute();
}

