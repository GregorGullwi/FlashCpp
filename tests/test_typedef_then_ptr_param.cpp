// Test with pointer parameter

extern "C" {
    typedef char* va_list;
    
    void test_func(int* ptr);
}

int main() {
    return 0;
}
