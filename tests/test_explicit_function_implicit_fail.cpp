// Test that implicit conversion with explicit constructor fails in function arguments
// This test should NOT compile - it's expected to fail

class Wrapper {
public:
    explicit Wrapper(int x) : value_(x) {}
    int value() const { return value_; }
private:
    int value_;
};

void takeWrapper(Wrapper w) {
    // Function that takes Wrapper by value
}

int main() {
    // This should fail - implicit conversion from int to Wrapper
    takeWrapper(10);  // ERROR: Cannot implicitly convert int to Wrapper
    return 0;
}
