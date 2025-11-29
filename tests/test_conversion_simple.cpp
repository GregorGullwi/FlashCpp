// Simpler test for conversion operators
struct MyInt {
    int value;

    MyInt(int v) : value(v) {}

    // Conversion operator to int
    operator int() const {
        return value;
    }
};

int main() {
    MyInt mi(42);
    int i = mi;  // Should call operator int()
    return i;
}
