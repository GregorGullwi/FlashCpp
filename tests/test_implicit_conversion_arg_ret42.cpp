// Test conversion operators in function arguments
struct MyInt {
    int value;

    MyInt(int v) : value(v) {}

    // Conversion operator to int
    operator int() const {
        return value;
    }
};

int addTen(int x) {
    return x + 10;
}

int main() {
    MyInt mi(32);
    // Should call conversion operator when passing to function
    int result = addTen(mi);  // Should convert mi to int first
    return result;  // Should return 42
}
