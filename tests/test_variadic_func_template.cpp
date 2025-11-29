// Test: Variadic function template with parameter pack expansion
template<typename... Args>
int sum(Args... args);  // Forward declaration with pack expansion

template<>
int sum<int>(int arg0) {
    return arg0;
}

template<>
int sum<int, int>(int arg0, int arg1) {
    return arg0 + arg1;
}

template<>
int sum<int, int, int>(int arg0, int arg1, int arg2) {
    return arg0 + arg1 + arg2;
}

int main() {
    int a = sum<int>(5);
    int b = sum<int, int>(3, 7);
    int c = sum<int, int, int>(1, 2, 4);
    return a + b + c - 22;  // 5 + 10 + 7 - 22 = 0
}
