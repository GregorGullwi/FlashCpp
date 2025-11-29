// Test if parameters work when used in calculations/returns

int add_level2(int a, int b, int c) {
    return a + b + c;
}

int add_level1(int a, int b, int c) {
    int result = add_level2(a, b, c);
    return result;
}

int main() {
    int result = add_level1(10, 20, 30);
    // result should be 60
    return (result == 60) ? 0 : 1;
}
