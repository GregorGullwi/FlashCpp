// Test copy elision for T x = T(args) pattern
// C++17 mandates copy elision in this case

class Counter {
public:
    Counter(int val) : value_(val) {
        // Constructor
    }
    
    Counter(const Counter& other) : value_(other.value_) {
        // Copy constructor - should NOT be called due to copy elision
        value_ += 1000;  // Add 1000 to detect if copy constructor was called
    }
    
    int value() const { return value_; }
    
private:
    int value_;
};

int main() {
    // Test 1: Copy elision with single argument
    Counter c1 = Counter(10);
    int v1 = c1.value();  // Should be 10, not 1010
    
    // Test 2: Copy elision with different value
    Counter c2 = Counter(20);
    int v2 = c2.value();  // Should be 20, not 1020
    
    // Test 3: Direct initialization (already works, but verify)
    Counter c3(30);
    int v3 = c3.value();  // Should be 30
    
    // If copy elision works, sum should be 10 + 20 + 30 = 60
    // If copy constructor is called, sum would be 1010 + 1020 + 30 = 2060
    int sum = v1 + v2 + v3;
    
    return (sum == 60) ? 0 : 1;
}
