// Test function pointer with nullptr (non-member)

// Simpler test: function pointer variable initialized to nullptr
int add(int a, int b) {
    return a + b;
}

int main() {
    int (*fp)(int, int) = nullptr;
    
    // Test that nullptr comparison works
    if (fp == nullptr) {
        // nullptr case - set fp to add function
        fp = add;
    }
    
    // Now fp should point to add
    int result = fp(3, 4);  // Should be 7
    
    return result - 7;  // Should return 0
}
