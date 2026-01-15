// Test that any pointer can implicitly convert to void*
// This is a standard C/C++ implicit conversion

void process_ptr(const void* ptr) {
    // Just a sink function - no implementation needed
}

int main() {
    const char* str = "hello";
    const int* nums = nullptr;
    const double* vals = nullptr;
    
    // All of these should compile - implicit conversion to void*
    process_ptr(str);
    process_ptr(nums);
    process_ptr(vals);
    
    return 0;
}
