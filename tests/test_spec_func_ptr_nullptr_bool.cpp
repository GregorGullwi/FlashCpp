// Test function pointer nullptr - very simple

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int);
    
    int testNull() {
        // Just set and check immediately, no comparison
        operation = nullptr;
        // Return 0 if operation is now 0 (nullptr should be 0)
        if (operation) {
            return 1;  // non-null
        }
        return 0;  // null
    }
};

int main() {
    Calculator<int*> calc;
    return calc.testNull();
}
