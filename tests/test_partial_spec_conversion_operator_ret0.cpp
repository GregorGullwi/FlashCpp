// Test: conversion operator in partial template specialization
// Validates fix for operator __pointer_type() in template<typename T> struct __atomic_base<T*>
// This test only verifies that the parser accepts the conversion operator syntax.

template<typename T>
struct Wrapper {
    T value;
};

template<typename T>
struct Wrapper<T*> {
private:
    typedef T* pointer_type;
    pointer_type ptr;
public:
    Wrapper() : ptr(nullptr) {}
    Wrapper(pointer_type p) : ptr(p) {}
    
    operator pointer_type() const {
        return ptr;
    }
    
    pointer_type get() const { return ptr; }
};

int main() {
    return 0;
}
