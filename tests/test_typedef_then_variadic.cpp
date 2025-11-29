// Test typedefs followed by variadic function in extern C

extern "C" {
    typedef unsigned long long uintptr_t;
    typedef char* va_list;
    
    void test_func(va_list*, ...);
}

int main() {
    return 0;
}
