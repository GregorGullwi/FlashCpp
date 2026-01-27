// Test just calling the setter without actual function pointer

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int);
    
    void setDummy(int x) {
        // Just do nothing
    }
};

int main() {
    Calculator<int*> calc;
    calc.setDummy(5);
    return 0;
}
