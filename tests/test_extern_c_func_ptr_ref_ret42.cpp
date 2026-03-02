// Test: extern "C" function reference uses unmangled C name (not C++ mangled name)
// when assigned to a function pointer variable.  The generated object must have
// an undefined reference to "add_c", not "_Z5add_cii".
extern "C" {
    int add_c(int a, int b) { return a + b; }
}
int main() {
    int (*fp)(int, int) = add_c;
    return fp(20, 22);  // Should return 42
}
