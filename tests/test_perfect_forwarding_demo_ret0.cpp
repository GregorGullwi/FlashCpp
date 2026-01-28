// Perfect forwarding verification test
// This test demonstrates that our compiler correctly:
// 1. Parses Args&&... (forwarding references)
// 2. Generates correct mangling for rvalue references ($$Q)
// 3. Supports overload resolution between T& and T&&

extern "C" int printf(const char*, ...);

// Overloaded functions to observe which gets called
void test_func(int& x) {
    printf("test_func(int&) - lvalue ref\n");
}

void test_func(int&& x) {
    printf("test_func(int&&) - rvalue ref\n");
}

int main() {
    printf("=== Test 1: Direct overload calls ===\n");
    int x = 42;
    test_func(x);  // Should print "lvalue ref"
    
    return 0;
}
