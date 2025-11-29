// Test file for conversion operators in FlashCpp
// Conversion operators allow implicit conversion from one type to another

struct MyInt {
    int value;

    MyInt(int v) : value(v) {}

    // Conversion operator to int
    operator int() const {
        return value;
    }

    // Conversion operator to double
    operator double() const {
        return static_cast<double>(value);
    }
};

int main() {
    MyInt mi(42);

    // Test implicit conversion to int
    int i = mi;  // Should call operator int()

    // Test implicit conversion to double
    double d = mi;  // Should call operator double()

    // Test in function calls that require conversion
    return i + static_cast<int>(d);
}