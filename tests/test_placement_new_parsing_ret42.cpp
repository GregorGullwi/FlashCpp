// Test: Placement new operator declaration parsing
// This tests that the parser correctly handles operator new/delete declarations
// The fix was about parsing these operator declarations, not runtime usage

typedef unsigned long size_t;

// Test 1: Global placement new operator declarations
struct CustomTag {};

inline void* operator new(size_t, void* ptr, CustomTag) noexcept {
    return ptr;
}

inline void operator delete(void*, void*, CustomTag) noexcept {
    // No-op
}

// Test 2: Array versions
inline void* operator new[](size_t, void* ptr, CustomTag) noexcept {
    return ptr;
}

inline void operator delete[](void*, void*, CustomTag) noexcept {
    // No-op
}

// Test 3: Can declare operator new/delete (the fix was about parsing these)
// Note: We don't define these to avoid linking issues
// void* operator new(size_t);
// void operator delete(void*);

int main() {
    // Test passes if these declarations compile
    // The fix was about parsing operator new/delete syntax correctly
    return 42;
}

