// Test explicit constructor with direct initialization - should pass
class Wrapper {
public:
    explicit Wrapper(int x) : value_(x) {}
    int value() const { return value_; }
private:
    int value_;
};

int main() {
    // Direct initialization with explicit constructor - should work
    Wrapper w1(42);
    
    // Another form of direct initialization - should work
    Wrapper w2{10};
    
    // Direct initialization via constructor - should work
    Wrapper w3 = Wrapper(20);
    
    return w1.value() + w2.value() + w3.value() - 72;  // 42 + 10 + 20 - 72 = 0
}
