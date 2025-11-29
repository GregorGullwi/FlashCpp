// Test using typedef'd type in parameter

extern "C" {
    typedef char* va_list;
    
    void test_func(va_list ptr);
}

int main() {
    return 0;
}
