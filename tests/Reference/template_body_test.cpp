// Template with function body - full test
// Tests that template instantiation generates correct code
template<typename T>
T max(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    int x = max(3, 5);
    int y = max(10, 7);
    return x + y;  // Should return 5 + 10 = 15
}

