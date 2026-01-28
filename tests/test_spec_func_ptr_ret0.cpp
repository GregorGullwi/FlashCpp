// Test template specialization with function pointer member

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

template<typename T>
struct Calculator;

// Specialization for int pointers
template<typename T>
struct Calculator<T*> {
    int value;
    int (*operation)(int, int);  // Function pointer at offset 8
    
    void setValue(int v) {
        value = v;
    }
    
    void setOperation(int (*op)(int, int)) {
        operation = op;
    }
    
    int execute(int x) {
        if (operation) {
            return operation(value, x);
        }
        return value;
    }
};

int main() {
    Calculator<int*> calc;
    
    calc.setValue(5);
    calc.setOperation(add);
    int result1 = calc.execute(3);  // 5 + 3 = 8
    
    calc.setOperation(multiply);
    int result2 = calc.execute(4);  // 5 * 4 = 20
    
    // Should return 8 + 20 - 28 = 0
    return result1 + result2 - 28;
}
