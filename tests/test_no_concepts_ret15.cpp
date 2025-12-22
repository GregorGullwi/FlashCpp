// Test without concepts

template<typename T>
T increment(T x) {
    return x + 1;
}

template<typename T>
T decrement(T x) {
    return x - 1;
}

int main() {
    int a = increment(5);
    int b = decrement(10);
    return a + b;
}
