// Simpler test - just one typedef then one function

extern "C" {
    typedef char* va_list;
    
    void test_func();
}

int main() {
    return 0;
}
