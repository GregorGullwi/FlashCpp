// Simple verification test
int test1() {
    int a = 1;
    int b = 2;
    int c = 3;
    return a + b + c;  // Expected: 6
}

int test2() {
    int a = 10;
    int b = 20;
    int c = 30;
    return a + b + c;  // Expected: 60
}

int main() {
    int r1 = test1();  // 6
    int r2 = test2();  // 60
    return r1 + r2;    // 66
}

