// Test nullptr with function pointer member

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int) = nullptr;
};

int main() {
    Calculator<int*> calc;
    return 0;
}
