// Test function declarations inside extern "C" block

extern "C" {
    typedef unsigned long long size_t;
    typedef long long intptr_t;
    
    // Simple function declaration
    void test_func();
    
    // Function with parameters
    int printf(const char* format, ...);
}

int main() {
    return 0;
}
