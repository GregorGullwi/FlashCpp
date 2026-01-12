// Test calling operator== as a member function by name
// This pattern is used in standard library headers like <typeinfo>

struct MyType {
    int value;
    
    bool operator==(const MyType& other) const {
        return value == other.value;
    }
    
    bool operator!=(const MyType& other) const {
        // Call operator== explicitly by name
        return !operator==(other);
    }
};

int main() {
    MyType a{42};
    MyType b{42};
    MyType c{10};
    
    // Test: a == b should be true, so a != b should be false
    if (a != b) return 1;  // Should not be true (a == b)
    
    // Test: a != c should be true (42 != 10)
    if (!(a != c)) return 2;  // Should be true (a != c)
    
    return 0;  // Success
}
