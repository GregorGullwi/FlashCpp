// Comprehensive test for conversion operators in FlashCpp
// This test demonstrates that basic conversion operators work correctly

struct MyInt {
    int value;

    MyInt(int v) : value(v) {}

    // Conversion operator to int
    operator int() const {
        return value;
    }
};

int main() {
    MyInt mi(42);
    
    // Test 1: Implicit conversion to int
    int i = mi;  // Should call operator int()
    
    // Test 2: Addition with converted value
    int j = 42;
    int result = i + j;  // Should be 84
    
    // Return result to verify correctness
    // Exit code should be 84
    return result;
}
