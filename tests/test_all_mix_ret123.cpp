int test_arithmetic() {
    int a = 10;
    int b = 5;
    int sum = a + b;
    int diff = a - b;
    int prod = a * b;
    int quot = a / b;
    int mod = a % b;
    return sum + diff + prod + quot + mod; // 15 + 5 + 50 + 2 + 0 = 72
}

// Test 2: Compound assignments
int test_compound_assignments() {
    int x = 100;
    x += 10;  // 110
    x -= 20;  // 90
    x *= 2;   // 180
    x /= 3;   // 60
    x %= 7;   // 4
    return x;
}

// Test 3: Increment/decrement operators
int test_inc_dec() {
    int a = 5;
    int b = 10;
    int r1 = a++;  // r1 = 5, a = 6
    int r2 = ++b;  // r2 = 11, b = 11
    int r3 = a--;  // r3 = 6, a = 5
    int r4 = --b;  // r4 = 10, b = 10
    return r1 + r2 + r3 + r4 + a + b; // 5 + 11 + 6 + 10 + 5 + 10 = 47
}

// Test 4: Comparisons and boolean literals
int test_comparisons() {
    int result = 0;
    if (5 < 10) result += 1;
    if (10 > 5) result += 2;
    if (5 <= 5) result += 4;
    if (10 >= 10) result += 8;
    if (5 == 5) result += 16;
    if (5 != 6) result += 32;
    if (true) result += 64;
    if (false) result += 128; // Should not execute
    return result; // 1 + 2 + 4 + 8 + 16 + 32 + 64 = 127
}

// Test 5: Logical operators
int test_logical() {
    int result = 0;
    if (true && true) result += 1;
    if (true && false) result += 2; // Should not execute
    if (false || true) result += 4;
    if (false || false) result += 8; // Should not execute
    return result; // 1 + 4 = 5
}

// Test 6: For loops
int test_for_loops() {
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += i;
    }
    return sum; // 0 + 1 + 2 + 3 + 4 = 10
}

// Test 7: While loops
int test_while_loops() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        sum += i;
        i++;
    }
    return sum; // 0 + 1 + 2 + 3 + 4 = 10
}

// Test 8: Do-while loops
int test_do_while_loops() {
    int sum = 0;
    int i = 0;
    do {
        sum += i;
        i++;
    } while (i < 5);
    return sum; // 0 + 1 + 2 + 3 + 4 = 10
}

// Test 9: Break statement
int test_break() {
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        if (i >= 5) break;
        sum += i;
    }
    return sum; // 0 + 1 + 2 + 3 + 4 = 10
}

// Test 10: Continue statement
int test_continue() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) continue; // Skip even numbers
        sum += i;
    }
    return sum; // 1 + 3 + 5 + 7 + 9 = 25
}

// Test 11: Nested loops with break/continue
int test_nested_loops() {
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (j == 2) break; // Break inner loop
            sum += i * j;
        }
    }
    return sum; // 0*0 + 0*1 + 1*0 + 1*1 + 2*0 + 2*1 = 0 + 0 + 0 + 1 + 0 + 2 = 3
}

// Test 12: If-else statements
int test_if_else() {
    int x = 10;
    int result = 0;
    
    if (x > 5) {
        result = 1;
    } else {
        result = 2;
    }
    
    if (x < 5) {
        result += 10;
    } else {
        result += 20;
    }
    
    return result; // 1 + 20 = 21
}

// Test 13: If with initializer (C++17 feature)
int test_if_with_init() {
    int result = 0;
    if (int x = 5; x > 3) {
        result = x * 2;
    }
    return result; // 10
}

// Test 14: For loop with all optional parts
int test_for_optional() {
    int sum = 0;
    int i = 0;
    for (; i < 5; ) {
        sum += i;
        i++;
    }
    return sum; // 0 + 1 + 2 + 3 + 4 = 10
}

// Test 15: Complex expression
int test_complex_expression() {
    int a = 5;
    int b = 3;
    int c = 2;
    int result = (a + b) * c - (a - b) / c; // (8) * 2 - (2) / 2 = 16 - 1 = 15
    return result;
}

int main() {
    int r1 = test_arithmetic();              // 72
    int r2 = test_compound_assignments();    // 4
    int r3 = test_inc_dec();                 // 47
    int r4 = test_comparisons();             // 127
    int r5 = test_logical();                 // 5
    int r6 = test_for_loops();               // 10
    int r7 = test_while_loops();             // 10
    int r8 = test_do_while_loops();          // 10
    int r9 = test_break();                   // 10
    int r10 = test_continue();               // 25
    int r11 = test_nested_loops();           // 3
    int r12 = test_if_else();                // 21
    int r13 = test_if_with_init();           // 10
    int r14 = test_for_optional();           // 10
    int r15 = test_complex_expression();     // 15
    
    return r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8 + r9 + r10 + r11 + r12 + r13 + r14 + r15;
    // 72 + 4 + 47 + 127 + 5 + 10 + 10 + 10 + 10 + 25 + 3 + 21 + 10 + 10 + 15 = 379
}

