// Test: C++ attributes between function name and parameter list
// func_name [[nodiscard]] (params) must parse correctly

[[nodiscard]] int compute(int x) {
    return x * 2;
}

int get_value [[nodiscard]] (int x) {
    return x + 1;
}

int main() {
    int a = compute(5);
    int b = get_value(10);
    return (a == 10 && b == 11) ? 0 : 1;
}
