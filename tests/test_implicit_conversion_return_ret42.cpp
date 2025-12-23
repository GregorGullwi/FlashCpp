// Test conversion operators in return statements
struct MyInt {
    int value;

    MyInt(int v) : value(v) {}

    // Conversion operator to int
    operator int() const {
        return value;
    }
};

int getValue() {
    MyInt mi(42);
    return mi;  // Should call conversion operator
}

int main() {
    return getValue();
}
