// C++20 Usual Arithmetic Conversions Tests
// Tests compliance with [conv.arith] for signed/unsigned conversions
//
// Expected return: 85

// Test 1: int + unsigned int -> unsigned int (same rank, unsigned wins)
unsigned int test_same_rank_unsigned_wins() {
    int i = 10;
    unsigned int u = 5;
    // Result is unsigned int
    return i + u;  // Returns 15
}

// Test 2: long + unsigned int -> depends on platform
// On most 64-bit systems: long can represent all unsigned int values -> long
// This test uses values that work regardless
int test_different_rank() {
    long l = 100;
    unsigned int u = 50;
    // Result is long (on 64-bit) because long can represent all unsigned int values
    return (int)(l - u);  // Returns 50
}

// Test 3: Mixed comparisons - different ranks, same signedness
int test_same_signedness_different_rank() {
    short s = 10;
    int i = 20;
    // short promotes to int, both are int, result is int
    return i - s;  // Returns 10
}

// Test 4: unsigned short + int -> int (int can represent all unsigned short values)
int test_unsigned_short_plus_int() {
    unsigned short us = 5;
    int i = 5;
    // unsigned short promotes to int (not unsigned int!)
    return us + i;  // Returns 10
}

int main() {
    int result = 0;
    
    // Test 1: same rank, unsigned wins - expects 15
    unsigned int r1 = test_same_rank_unsigned_wins();
    if (r1 != 15) return 1;
    result += 20;  // +20 if passed
    
    // Test 2: different rank - expects 50
    int r2 = test_different_rank();
    if (r2 != 50) return 2;
    result += 20;  // +20 if passed
    
    // Test 3: same signedness different rank - expects 10
    int r3 = test_same_signedness_different_rank();
    if (r3 != 10) return 3;
    result += 20;  // +20 if passed
    
    // Test 4: unsigned short + int - expects 10
    int r4 = test_unsigned_short_plus_int();
    if (r4 != 10) return 4;
    result += 25;  // +25 if passed
    
    return result;  // Should return 85
}
