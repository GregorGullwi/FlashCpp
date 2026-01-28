// Test new compiler intrinsics
// Tests: __is_reference, __is_arithmetic, __is_fundamental, __is_object,
//        __is_scalar, __is_compound, __is_convertible, __is_const, __is_volatile,
//        __is_signed, __is_unsigned, __is_bounded_array, __is_unbounded_array

struct MyStruct {
    int value;
};

enum MyEnum { A, B, C };

// Test __is_reference
int test_is_reference() {
    int result = 0;
    
    // These should be true
    if (__is_reference(int&)) result += 1;
    if (__is_reference(int&&)) result += 2;
    if (__is_reference(const int&)) result += 4;
    
    // These should be false
    if (!__is_reference(int)) result += 8;
    if (!__is_reference(int*)) result += 16;
    if (!__is_reference(const int)) result += 32;
    
    return result;  // Expected: 63
}

// Test __is_arithmetic
int test_is_arithmetic() {
    int result = 0;
    
    // These should be true
    if (__is_arithmetic(int)) result += 1;
    if (__is_arithmetic(float)) result += 2;
    if (__is_arithmetic(double)) result += 4;
    if (__is_arithmetic(char)) result += 8;
    if (__is_arithmetic(bool)) result += 16;
    
    // These should be false
    if (!__is_arithmetic(int*)) result += 32;
    if (!__is_arithmetic(MyStruct)) result += 64;
    if (!__is_arithmetic(void)) result += 128;
    
    return result;  // Expected: 255
}

// Test __is_fundamental
int test_is_fundamental() {
    int result = 0;
    
    // These should be true
    if (__is_fundamental(int)) result += 1;
    if (__is_fundamental(float)) result += 2;
    if (__is_fundamental(void)) result += 4;
    if (__is_fundamental(bool)) result += 8;
    
    // These should be false
    if (!__is_fundamental(int*)) result += 16;
    if (!__is_fundamental(MyStruct)) result += 32;
    if (!__is_fundamental(MyEnum)) result += 64;
    
    return result;  // Expected: 127
}

// Test __is_object
int test_is_object() {
    int result = 0;
    
    // These should be true
    if (__is_object(int)) result += 1;
    if (__is_object(int*)) result += 2;
    if (__is_object(MyStruct)) result += 4;
    if (__is_object(MyEnum)) result += 8;
    
    // These should be false
    if (!__is_object(void)) result += 16;
    if (!__is_object(int&)) result += 32;
    
    return result;  // Expected: 63
}

// Test __is_scalar
int test_is_scalar() {
    int result = 0;
    
    // These should be true
    if (__is_scalar(int)) result += 1;
    if (__is_scalar(float)) result += 2;
    if (__is_scalar(int*)) result += 4;
    if (__is_scalar(MyEnum)) result += 8;
    if (__is_scalar(bool)) result += 16;
    
    // These should be false
    if (!__is_scalar(MyStruct)) result += 32;
    if (!__is_scalar(int&)) result += 64;
    if (!__is_scalar(void)) result += 128;
    
    return result;  // Expected: 255
}

// Test __is_compound
int test_is_compound() {
    int result = 0;
    
    // These should be true
    if (__is_compound(MyStruct)) result += 1;
    if (__is_compound(int*)) result += 2;
    if (__is_compound(int&)) result += 4;
    if (__is_compound(MyEnum)) result += 8;
    
    // These should be false
    if (!__is_compound(int)) result += 16;
    if (!__is_compound(float)) result += 32;
    if (!__is_compound(void)) result += 64;
    
    return result;  // Expected: 127
}

// Test __is_convertible
int test_is_convertible() {
    int result = 0;
    
    // These should be true
    if (__is_convertible(int, int)) result += 1;
    if (__is_convertible(int, float)) result += 2;
    if (__is_convertible(float, double)) result += 4;
    if (__is_convertible(char, int)) result += 8;
    
    // These should be false
    if (!__is_convertible(int*, float)) result += 16;
    if (!__is_convertible(MyStruct, int)) result += 32;
    
    return result;  // Expected: 63
}

// Test __is_const
int test_is_const() {
    int result = 0;
    
    // These should be true
    if (__is_const(const int)) result += 1;
    if (__is_const(const float)) result += 2;
    if (__is_const(const MyStruct)) result += 4;
    
    // These should be false
    if (!__is_const(int)) result += 8;
    if (!__is_const(float)) result += 16;
    if (!__is_const(MyStruct)) result += 32;
    
    return result;  // Expected: 63
}

// Test __is_volatile
int test_is_volatile() {
    int result = 0;
    
    // These should be true
    if (__is_volatile(volatile int)) result += 1;
    if (__is_volatile(volatile float)) result += 2;
    
    // These should be false
    if (!__is_volatile(int)) result += 4;
    if (!__is_volatile(const int)) result += 8;
    if (!__is_volatile(float)) result += 16;
    
    return result;  // Expected: 31
}

// Test __is_signed
int test_is_signed() {
    int result = 0;
    
    // These should be true
    if (__is_signed(int)) result += 1;
    if (__is_signed(short)) result += 2;
    if (__is_signed(long)) result += 4;
    if (__is_signed(char)) result += 8;
    
    // These should be false
    if (!__is_signed(unsigned int)) result += 16;
    if (!__is_signed(bool)) result += 32;
    if (!__is_signed(float)) result += 64;
    
    return result;  // Expected: 127
}

// Test __is_unsigned
int test_is_unsigned() {
    int result = 0;
    
    // These should be true
    if (__is_unsigned(unsigned int)) result += 1;
    if (__is_unsigned(unsigned short)) result += 2;
    if (__is_unsigned(unsigned long)) result += 4;
    if (__is_unsigned(bool)) result += 8;
    
    // These should be false
    if (!__is_unsigned(int)) result += 16;
    if (!__is_unsigned(float)) result += 32;
    if (!__is_unsigned(char)) result += 64;
    
    return result;  // Expected: 127
}

// Test __is_bounded_array
int test_is_bounded_array() {
    int result = 0;
    
    // These should be true
    if (__is_bounded_array(int[10])) result += 1;
    if (__is_bounded_array(char[5])) result += 2;
    if (__is_bounded_array(MyStruct[3])) result += 4;
    
    // These should be false
    if (!__is_bounded_array(int)) result += 8;
    if (!__is_bounded_array(int*)) result += 16;
    
    return result;  // Expected: 31
}

// Test __is_unbounded_array
int test_is_unbounded_array() {
    int result = 0;
    
    // These should be true
    if (__is_unbounded_array(int[])) result += 1;
    if (__is_unbounded_array(char[])) result += 2;
    
    // These should be false
    if (!__is_unbounded_array(int[10])) result += 4;
    if (!__is_unbounded_array(int)) result += 8;
    if (!__is_unbounded_array(int*)) result += 16;
    
    return result;  // Expected: 31
}

int main() {
    int total = 0;
    
    total += test_is_reference();            // +63
    total += test_is_arithmetic();           // +255
    total += test_is_fundamental();          // +127
    total += test_is_object();               // +63
    total += test_is_scalar();               // +255
    total += test_is_compound();             // +127
    total += test_is_convertible();          // +63
    total += test_is_const();                // +63
    total += test_is_volatile();             // +31
    total += test_is_signed();               // +127
    total += test_is_unsigned();             // +127
    total += test_is_bounded_array();        // +31
    total += test_is_unbounded_array();      // +31
    
    // Expected total: 63 + 255 + 127 + 63 + 255 + 127 + 63 + 63 + 31 + 127 + 127 + 31 + 31 = 1363
    if (total == 1363) {
        return 0;  // Success
    }
    return 1;  // Failure
}
