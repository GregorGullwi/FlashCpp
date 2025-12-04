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

int test_has_unique_object_representations() {
    int result = 0;
    // Integer types have unique object representations
    if (__has_unique_object_representations(int)) result += 1;
    if (__has_unique_object_representations(char)) result += 2;
    if (__has_unique_object_representations(long)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_constructible(T, Args...)
int test_is_constructible() {
    int result = 0;
    // Scalars are default constructible
    if (__is_constructible(int)) result += 1;
    if (__is_constructible(double)) result += 2;
    // Scalars are copy constructible
    if (__is_constructible(int, int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_assignable(To, From)
int test_is_assignable() {
    int result = 0;
    // Scalars are assignable
    if (__is_assignable(int, int)) result += 1;
    if (__is_assignable(double, double)) result += 2;
    if (__is_assignable(int, double)) result += 4;
    
    return result;  // Expected: 7
}

int main() {
    int total = 0;
    
    total += test_is_void();                   // +1
    total += test_is_integral();               // +127
    total += test_has_unique_object_representations(); // +7
    total += test_is_constructible();          // +7
    total += test_is_assignable();             // +7
    
    return total;  // Expected: 149
}
