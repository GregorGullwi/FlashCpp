// Test addition with converted values
struct MyInt {
    int value;

    MyInt(int v) : value(v) {}

    operator int() const {
        return value;
    }
};

int main() {
    MyInt mi(42);
    int i = mi;
    int j = 42;
    return i + j;  // Should return 84
}
