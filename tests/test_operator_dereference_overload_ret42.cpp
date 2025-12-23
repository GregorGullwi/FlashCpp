// Test operator* (dereference) overload resolution
// This demonstrates iterator-like dereference behavior

struct IntPointer {
    int* ptr;
    
    // Dereference operator
    int& operator*() {
        return *ptr;
    }
};

int main() {
    int value = 42;
    IntPointer ip;
    ip.ptr = &value;
    
    // Dereference using operator*
    int result = *ip;
    
    return result;  // Should return 42
}
