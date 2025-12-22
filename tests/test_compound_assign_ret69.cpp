// Test compound assignment operators

int test_add_assign() {
    int a = 10;
    a += 5;
    return a; // Should be 15
}

int test_sub_assign() {
    int a = 20;
    a -= 7;
    return a; // Should be 13
}

int test_mul_assign() {
    int a = 6;
    a *= 4;
    return a; // Should be 24
}

int test_div_assign() {
    int a = 50;
    a /= 5;
    return a; // Should be 10
}

int test_mod_assign() {
    int a = 17;
    a %= 5;
    return a; // Should be 2
}

int test_multiple_compound() {
    int a = 10;
    a += 5;  // 15
    a *= 2;  // 30
    a -= 10; // 20
    a /= 4;  // 5
    return a;
}

int main() {
    int r1 = test_add_assign();
    int r2 = test_sub_assign();
    int r3 = test_mul_assign();
    int r4 = test_div_assign();
    int r5 = test_mod_assign();
    int r6 = test_multiple_compound();
    return r1 + r2 + r3 + r4 + r5 + r6;
    // 15 + 13 + 24 + 10 + 2 + 5 = 69
}

