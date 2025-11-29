// Comprehensive test of parameter name flexibility

// Test 1: Unnamed parameters in declaration
void test1(int, double);
void test1(int x, double y) { }

// Test 2: Different names in declaration vs definition
void test2(int foo, int bar);
void test2(int x, int y) { }

// Test 3: Const differences
void test3(const int x);
void test3(int x) { }

void test4(int x);
void test4(const int x) { }

// Test 5: Pointer const differences
void test5(const int* p);
void test5(const int* p) { }

// Test 6: Mixed named/unnamed in declaration
void test6(int x, int, double);
void test6(int a, int b, double c) { }

// Test 7: All unnamed in declaration
void test7(int, int, int);
void test7(int a, int b, int c) { }

// Test 8: Typedef'd types with unnamed parameters
typedef char* string_t;
void test8(string_t, int);
void test8(string_t s, int x) { }

// Test 9: Pointer to typedef with unnamed parameter
typedef int* intptr_t;
void test9(intptr_t*, int);
void test9(intptr_t* p, int x) { }

// Test 10: Variadic with unnamed first parameter
void test10(int, ...);
void test10(int x, ...) { }

int main() {
    return 0;
}
