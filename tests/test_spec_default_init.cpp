// Test default initializers in template specialization

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int value = 0;
    int (*operation)(int, int) = nullptr;
};

int main() {
    Calculator<int*> calc;
    return 0;
}
