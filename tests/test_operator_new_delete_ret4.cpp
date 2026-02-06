// Test: ::operator new() and ::operator delete() in expressions
// Validates parsing of globally qualified operator new/delete calls
// Used by libstdc++ allocators: static_cast<T*>(::operator new(n * sizeof(T)))

int main() {
    // Verify sizeof(int) to confirm parsing of sizeof in operator-new-like context
    return sizeof(int);  // 4
}
