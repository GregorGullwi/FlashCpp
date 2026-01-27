// Minimal test: just declare the struct with function pointer

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int (*operation)(int, int);
};

int main() {
    Calculator<int*> calc;
    return 0;
}
