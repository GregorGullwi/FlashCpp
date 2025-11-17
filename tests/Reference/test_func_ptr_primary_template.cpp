// Test function pointer in primary template (not specialization)

template<typename T>
struct Calculator {
    int (*operation)(int, int);
};

int main() {
    Calculator<int> c;
    return 0;
}
