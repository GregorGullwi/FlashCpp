// Test compiler intrinsic type traits
// These are compiler built-ins that can be used by <type_traits>

// Test struct/class for __is_class
struct MyStruct {
    int value;
};

class MyClass {
public:
    int value;
};

// Test enum for __is_enum
enum Color { Red, Green, Blue };
enum class ScopedColor { Red, Green, Blue };

// Test function returning type trait results
int test_is_void() {
    // __is_void(void) should be true
    return __is_void(void) ? 1 : 0;
}

int test_is_integral() {
    int result = 0;
    // These should all be true
    if (__is_integral(int)) result += 1;
    if (__is_integral(char)) result += 2;
    if (__is_integral(bool)) result += 4;
    if (__is_integral(long)) result += 8;
    if (__is_integral(unsigned int)) result += 16;
    
    // These should be false
    if (!__is_integral(float)) result += 32;
    if (!__is_integral(double)) result += 64;
    
    return result;  // Expected: 127 (all tests pass)
}

int test_is_floating_point() {
    int result = 0;
    // These should be true
    if (__is_floating_point(float)) result += 1;
    if (__is_floating_point(double)) result += 2;
    
    // These should be false
    if (!__is_floating_point(int)) result += 4;
    if (!__is_floating_point(char)) result += 8;
    
    return result;  // Expected: 15
}

int test_is_pointer() {
    int result = 0;
    // These should be true
    if (__is_pointer(int*)) result += 1;
    if (__is_pointer(void*)) result += 2;
    if (__is_pointer(MyStruct*)) result += 4;
    
    // These should be false
    if (!__is_pointer(int)) result += 8;
    if (!__is_pointer(MyStruct)) result += 16;
    
    return result;  // Expected: 31
}

int test_is_reference() {
    int result = 0;
    // Test lvalue references
    if (__is_lvalue_reference(int&)) result += 1;
    
    // Test rvalue references  
    if (__is_rvalue_reference(int&&)) result += 2;
    
    // These should be false for lvalue
    if (!__is_lvalue_reference(int)) result += 4;
    if (!__is_lvalue_reference(int*)) result += 8;
    
    return result;  // Expected: 15
}

int test_is_class() {
    int result = 0;
    // These should be true
    if (__is_class(MyStruct)) result += 1;
    if (__is_class(MyClass)) result += 2;
    
    // These should be false
    if (!__is_class(int)) result += 4;
    if (!__is_class(float)) result += 8;
    if (!__is_class(Color)) result += 16;
    
    return result;  // Expected: 31
}

int test_is_enum() {
    int result = 0;
    // These should be true
    if (__is_enum(Color)) result += 1;
    if (__is_enum(ScopedColor)) result += 2;
    
    // These should be false
    if (!__is_enum(int)) result += 4;
    if (!__is_enum(MyStruct)) result += 8;
    
    return result;  // Expected: 15
}

int main() {
    int total = 0;
    
    total += test_is_void();           // +1
    total += test_is_integral();       // +127
    total += test_is_floating_point(); // +15
    total += test_is_pointer();        // +31
    total += test_is_reference();      // +15
    total += test_is_class();          // +31
    total += test_is_enum();           // +15
    
    return total;  // Expected: 235
}
