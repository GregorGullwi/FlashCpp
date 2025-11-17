// Test nullptr check in if statement

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int);
    
    int check() {
        if (operation == nullptr) {
            return 0;
        }
        return 1;
    }
};

int main() {
    Calculator<int*> calc;
    calc.operation = nullptr;
    // Should return 0 if nullptr check works
    return calc.check();
}
