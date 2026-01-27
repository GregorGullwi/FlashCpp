// Test if using typedef'ed type is the issue

extern "C" {
    typedef unsigned long long uintptr_t;
    typedef char* va_list;
    
    // Use a different type, not the typedef'ed one
    void test_func(int*, ...);
}

int main() {
    return 0;
}
