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

int main() {
    return 0;
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
