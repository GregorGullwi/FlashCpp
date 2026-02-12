// Test explicit constructors with direct initialization in function arguments and return values
// Direct initialization should work even with explicit constructors

class Wrapper {
public:
    explicit Wrapper(int x) : value_(x) {}
    int value() const { return value_; }
private:
    int value_;
};

// Test 1: Direct initialization via function argument with explicit temporary
void takeWrapper(Wrapper w) {
    // Function should accept explicitly constructed temporary
}

// Test 2: Return value with explicit constructor
Wrapper makeWrapper(int x) {
    return Wrapper(x);  // Direct initialization - should work
}

// Test 3: Function taking wrapper and returning result
int processWrapper(Wrapper w) {
    return w.value();
}

int main() {
    // Test 1: Pass explicitly constructed temporary to function
    takeWrapper(Wrapper(10));  // OK: direct initialization with explicit temporary
    
    // Test 2: Return value construction
    Wrapper w1 = makeWrapper(20);  // OK: return value optimization / move
    
    // Test 3: Chained function calls with explicit construction
    int result = processWrapper(Wrapper(30));  // OK: direct initialization in argument
    
    // Test 4: Direct initialization in variable declaration
    Wrapper w2(40);  // OK: direct initialization
    Wrapper w3{50};  // OK: direct list initialization
    
    // Verify all values are correct
    int sum = w1.value() + result + w2.value() + w3.value();
    // Expected: 20 + 30 + 40 + 50 = 140
    
    return sum == 140 ? 0 : 1;
}
