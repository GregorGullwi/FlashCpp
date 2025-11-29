// Test that nullptr initialization works properly

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int value = 42;
    int (*operation)(int, int) = nullptr;
    
    int getValue() {
        return value;
    }
};

int main() {
    Calculator<int*> calc;
    // Should return 42 - 42 = 0
    return calc.getValue() - 42;
}
