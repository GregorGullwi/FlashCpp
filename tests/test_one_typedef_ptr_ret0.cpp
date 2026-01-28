// Test with just one typedef before function using it

extern "C" {
    typedef char* va_list;
    
    void test_func(va_list* ptr);
}

int main() {
    return 0;
}
