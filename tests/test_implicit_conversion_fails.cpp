// Test that demonstrates the need for proper implicit conversion sequences
// This test should fail if conversion operators aren't called properly

struct MyInt {
    int value;
    int marker;  // Extra member to make the struct larger than int

    MyInt(int v) : value(v), marker(99) {}

    // Conversion operator to int
    operator int() const {
        return value;  // Should return just the value, not the whole struct
    }
};

int main() {
    MyInt mi(42);
    int i = mi;  // Should call operator int() and get 42, not try to memcpy the struct
    return i;
}
