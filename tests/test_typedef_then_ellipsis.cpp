// Test variadic after typedef

extern "C" {
    typedef char* va_list;
    
    void test_func(int x, ...);
}

int main() {
    return 0;
}
