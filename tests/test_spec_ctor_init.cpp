// Test with constructor that applies default values

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int value;
    int (*operation)(int, int);
    
    Calculator() {
        value = 42;
        operation = nullptr;
    }
};

int main() {
    Calculator<int*> calc;
    // Should return 42 - 42 = 0
    return calc.value - 42;
}
