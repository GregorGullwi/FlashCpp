// Test that demonstrates the need for proper implicit conversion sequences
// This test ensures conversion operators are called instead of direct memory copy
// Without proper conversion operator calls, this would attempt to copy the entire
// 8-byte struct into a 4-byte int variable, leading to incorrect results.

struct MyInt {
    int value;
    int marker;  // Extra member to make the struct larger than int

    MyInt(int v) : value(v), marker(99) {}

    // Conversion operator to int
    // This should be called to extract just the 'value' member
    operator int() const {
        return value;  // Should return just the value, not the whole struct
    }
};

int main() {
    MyInt mi(42);
    int i = mi;  // Should call operator int() and get 42, not try to memcpy the struct
    return i;
}
