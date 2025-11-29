// Test: Variadic function template with actual usage
template<typename T>
T identity(T value) {
    return value;
}

template<typename T1, typename T2>
T1 first(T1 a, T2 b) {
    return a;
}

int main() {
    int x = identity<int>(42);
    int y = first<int, float>(10, 3.14f);
    return x + y - 52;  // 42 + 10 - 52 = 0
}
