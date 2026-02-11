// Comprehensive test for explicit constructor functionality
// Tests various scenarios with explicit constructors

class ExplicitInt {
public:
    explicit ExplicitInt(int x) : value_(x) {}
    int value() const { return value_; }
private:
    int value_;
};

class NonExplicitInt {
public:
    NonExplicitInt(int x) : value_(x) {}  // Implicit conversion allowed
    int value() const { return value_; }
private:
    int value_;
};

// Test helper that takes ExplicitInt by value
int useExplicitInt(ExplicitInt e) {
    return e.value();
}

// Test helper that takes NonExplicitInt by value
int useNonExplicitInt(NonExplicitInt n) {
    return n.value();
}

int main() {
    // ===== Test 1: Direct initialization with explicit - should work =====
    ExplicitInt e1(10);  // OK: direct initialization
    ExplicitInt e2{20};  // OK: direct list initialization
    ExplicitInt e3 = ExplicitInt(30);  // OK: explicit conversion then copy
    
    // ===== Test 2: Direct initialization with non-explicit - should work =====
    NonExplicitInt n1(5);  // OK: direct initialization
    NonExplicitInt n2{15}; // OK: direct list initialization
    NonExplicitInt n3 = 25;  // OK: copy initialization with implicit conversion
    NonExplicitInt n4 = NonExplicitInt(35);  // OK: explicit conversion then copy
    
    // ===== Test 3: Function arguments =====
    int r1 = useExplicitInt(ExplicitInt(40));  // OK: explicit conversion
    int r2 = useNonExplicitInt(45);  // OK: implicit conversion
    int r3 = useNonExplicitInt(NonExplicitInt(50));  // OK: explicit conversion
    
    // Calculate result: 10+20+30 + 5+15+25+35 + 40+45+50 = 275
    return e1.value() + e2.value() + e3.value() + 
           n1.value() + n2.value() + n3.value() + n4.value() +
           r1 + r2 + r3 - 275;  // Should return 0
}
