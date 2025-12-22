// Simple test of default initializer

template<typename T>
struct Calculator;

template<typename T>
struct Calculator<T*> {
    int value = 42;
};

int main() {
    Calculator<int*> calc{};  // Use {} to value-initialize
    return calc.value - 42;
}
