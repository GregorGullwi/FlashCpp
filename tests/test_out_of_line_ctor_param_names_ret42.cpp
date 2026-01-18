// Test: constructor parameter names differ between declaration and definition
// In C++, this is perfectly valid - only the types must match
// The member initializer list should use the definition's parameter names

struct Test {
    int value;
    Test(int x);  // Declaration uses "x"
};

Test::Test(int y) : value(y) {  // Definition uses "y", initializer uses "y"
}

int main() {
    Test t(42);
    return t.value;  // Should return 42
}
