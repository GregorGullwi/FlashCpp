struct Test {
    int getValue() { return 42; }  // Declared BEFORE constructor
    Test() : value(getValue()) {}  // Now calls getValue
    int value;
};

int main() {
    Test t;
    return t.value;  // Should return 42
}
