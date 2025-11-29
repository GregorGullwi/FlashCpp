// Test accessing function pointer member

int add(int a, int b) {
    return a + b;
}

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int);
    
    void setOperation(int (*op)(int, int)) {
        operation = op;
    }
};

int main() {
    Calculator<int*> calc;
    calc.setOperation(add);
    return 0;
}
