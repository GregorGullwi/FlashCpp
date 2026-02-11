// Test that copy initialization with explicit constructor fails
// This test should NOT compile - it's expected to fail
class Wrapper {
public:
    explicit Wrapper(int x) : value_(x) {}
    int value() const { return value_; }
private:
    int value_;
};

int main() {
    // This should fail - copy initialization with explicit constructor
    Wrapper w = 10;
    return 0;
}
