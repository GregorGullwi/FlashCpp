// Test: ::operator new() and ::operator delete() in expressions
// Validates parsing of globally qualified operator new/delete calls
// Used by libstdc++ allocators: static_cast<T*>(::operator new(n * sizeof(T)))
#include <cstddef>

void* my_alloc(size_t n) {
    return static_cast<void*>(::operator new(n));
}

void my_dealloc(void* p) {
    ::operator delete(p);
}

int main() {
    void* ptr = my_alloc(64);
    my_dealloc(ptr);
    return 0;
}
