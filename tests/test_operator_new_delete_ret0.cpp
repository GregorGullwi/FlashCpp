// Test operator new and operator delete parsing at global scope
// This pattern is used in <new> header
// We use placement new with a tag type to avoid linker conflicts with CRT

// Forward declare size_t - use __SIZE_TYPE__ for portability across compilers
typedef __SIZE_TYPE__ size_t;

// Tag type for our custom placement new
struct MyTag {};

// Test that operator new/delete NAMES can be parsed
// Using inline placement new syntax - no external dependencies
inline void* operator new(size_t, void* ptr, MyTag) noexcept {
    return ptr;  // Simple placement - just return the pointer
}

inline void operator delete(void*, void*, MyTag) noexcept {
    // No-op for placement delete
}

// Array versions
inline void* operator new[](size_t, void* ptr, MyTag) noexcept {
    return ptr;
}

inline void operator delete[](void*, void*, MyTag) noexcept {
    // No-op
}

int main() {
    // Basic test - operator new/delete declarations should parse without error
    return 0;
}
