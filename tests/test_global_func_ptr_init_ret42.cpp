// Test: global function pointer initialized with function address at global scope.
// The compiler must emit a data relocation (R_X86_64_64) so the linker fills in
// the correct function address, and must treat the call as an indirect call
// through the pointer rather than a direct function call.
int add(int a, int b) { return a + b; }
int (*global_func_ptr)(int, int) = add;
int main() {
    return global_func_ptr(20, 22);  // Should return 42
}
