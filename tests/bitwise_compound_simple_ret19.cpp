// Test compound assignments one at a time
int test_and_assign() {
    int a = 15;
    a &= 7;
    return a;  // Should be 7
}

int test_or_assign() {
    int b = 8;
    b |= 4;
    return b;  // Should be 12
}

int main() {
    return test_and_assign() + test_or_assign();  // 7 + 12 = 19
}
