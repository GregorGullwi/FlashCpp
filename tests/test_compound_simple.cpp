// Simpler compound assignment test

int test_add_assign() {
    int a = 10;
    a += 5;
    return a; // Should be 15
}

int main() {
    return test_add_assign();
}
