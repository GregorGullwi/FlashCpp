// Test: parameter names differ between declaration and definition in out-of-line member functions
// In C++, this is perfectly valid - only the types must match
// The function body should use the definition's parameter names

struct Test {
    int func(int x);  // Declaration uses "x"
};

int Test::func(int y) {  // Definition uses "y"
    return y + 1;  // Should access "y" not "x"
}

int main() {
    Test t;
    return t.func(41);  // Should return 42
}
