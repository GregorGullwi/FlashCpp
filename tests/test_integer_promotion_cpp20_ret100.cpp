// C++20 Integer Promotion and Usual Arithmetic Conversion Tests
// Tests compliance with [conv.prom] and [conv.arith]
//
// Expected return: 100

// Test 1: Basic integer promotions
// char + char should promote both to int before operation
char test_char_promotion() {
    char a = 50;
    char b = 40;
    // Both promoted to int, addition done as int, result truncated to char
    return a + b;  // Returns 90
}

// Test 2: short + short promotion
short test_short_promotion() {
    short a = 5;
    short b = 5;
    // Both promoted to int, multiplication done as int, result truncated to short
    return a * b;  // Returns 25
}

// Test 3: Mixed char and short (both promote to int)
int test_mixed_char_short() {
    char c = 10;
    short s = 5;
    // Both promote to int, subtraction done as int
    return c - s;  // Returns 5
}

// Test 4: bool to int promotion
int test_bool_promotion() {
    bool t = true;
    bool f = false;
    // Both promoted to int (true -> 1, false -> 0)
    return t + t + f;  // Returns 2
}

// Test 5: Unsigned char promotion (should promote to int, not unsigned int)
// since int can represent all unsigned char values
int test_unsigned_char_promotion() {
    unsigned char uc1 = 100;
    unsigned char uc2 = 50;
    // Both promoted to int (not unsigned int)
    return uc1 - uc2;  // Returns 50
}

int main() {
    int result = 0;
    
    // Test 1: char promotion - expects 90
    char r1 = test_char_promotion();
    if (r1 != 90) return 1;
    result += 20;  // +20 if passed
    
    // Test 2: short promotion - expects 25
    short r2 = test_short_promotion();
    if (r2 != 25) return 2;
    result += 20;  // +20 if passed
    
    // Test 3: mixed char/short - expects 5
    int r3 = test_mixed_char_short();
    if (r3 != 5) return 3;
    result += 20;  // +20 if passed
    
    // Test 4: bool promotion - expects 2
    int r4 = test_bool_promotion();
    if (r4 != 2) return 4;
    result += 20;  // +20 if passed
    
    // Test 5: unsigned char promotion - expects 50
    int r5 = test_unsigned_char_promotion();
    if (r5 != 50) return 5;
    result += 20;  // +20 if passed
    
    return result;  // Should return 100
}
