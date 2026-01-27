// Minimal overload resolution test with constructor

extern "C" int printf(const char*, ...);

// Overloads
void test(int& x) {
    printf("lvalue\n");
}

void test(int&& x) {
    printf("rvalue\n");
}

int main() {
    int a = 5;
    test(a);  // lvalue
    
    return 0;
}
