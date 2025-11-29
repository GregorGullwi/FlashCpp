// Simple printf test to verify parameter passing

extern "C" int printf(const char*, ...);

void test_func(int a, int b, int c) {
    printf("test_func: a=%d, b=%d, c=%d\n", a, b, c);
}

int main() {
    printf("Test 1: Direct call\n");
    test_func(1, 2, 3);
    
    printf("\nTest 2: Another call\n");
    test_func(10, 20, 30);
    
    return 0;
}
