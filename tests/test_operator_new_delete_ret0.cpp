// Test operator new and operator delete at global scope
// This pattern is used in <new> header

#include <stddef.h>

// Forward declarations for operator new/delete
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* ptr);
void operator delete[](void* ptr);

// Placement new (C++ requires this signature)
void* operator new(size_t size, void* ptr) noexcept;

int main() {
    // Basic test - declarations should parse without error
    return 0;
}
