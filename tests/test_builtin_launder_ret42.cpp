// Test __builtin_launder intrinsic
// This creates an optimization barrier for pointers

int main() {
    int x = 42;
    int* p = &x;
    
    // __builtin_launder returns the pointer unchanged
    // but prevents compiler from making assumptions
    int* laundered = __builtin_launder(p);
    
    return *laundered;  // Expected: 42
}
