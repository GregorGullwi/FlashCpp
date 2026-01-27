// Test setting nullptr and checking in member function

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int);
    
    void reset() {
        operation = nullptr;
    }
    
    int isNull() {
        if (operation == nullptr) {
            return 0;
        }
        return 1;
    }
};

int main() {
    Calculator<int*> calc;
    calc.reset();
    // Should return 0 if nullptr assignment and check work
    return calc.isNull();
}
