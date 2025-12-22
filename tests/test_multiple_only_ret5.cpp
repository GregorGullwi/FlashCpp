// Test multiple compound operations

int test_multiple_compound() {
    int a = 10;
    a += 5;  // 15
    a *= 2;  // 30
    a -= 10; // 20
    a /= 4;  // 5
    return a;
}

int main() {
    return test_multiple_compound();
}
