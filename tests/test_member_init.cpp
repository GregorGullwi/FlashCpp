// Test struct member initialization including function pointers

int globalFunc() {
    return 10;
}

struct TestInit {
    int regular = 42;
    int* ptr = nullptr;
    int (*func_ptr)() = nullptr;
    int (*func_ptr_assigned)() = globalFunc;
};

int main() {
    TestInit t;
    
    // Test regular initialization
    int val1 = t.regular;  // Should be 42
    
    // Test nullptr initialization
    int val2 = (t.ptr == nullptr) ? 1 : 0;  // Should be 1
    int val3 = (t.func_ptr == nullptr) ? 1 : 0;  // Should be 1
    
    // Test function pointer initialization
    int val4 = 0;
    if (t.func_ptr_assigned != nullptr) {
        val4 = t.func_ptr_assigned();  // Should be 10
    }
    
    return val1 + val2 + val3 + val4;  // 42 + 1 + 1 + 10 = 54
}
