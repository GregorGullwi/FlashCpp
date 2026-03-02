// Test: global function pointer initialized with function address at global scope.
// The compiler must emit a data relocation (R_X86_64_64) so the linker fills in
// the correct function address, and must treat the call as an indirect call
// through the pointer rather than a direct function call.
// Also tests copy-initialization from another global function pointer variable.
int add(int a, int b) { return a + b; }
int (*global_func_ptr)(int, int) = add;
int (*global_func_ptr2)(int, int) = global_func_ptr;  // copy-init from another global fp
int main() {
    // Both pointers must resolve to add(), so both calls must return 42.
    int r1 = global_func_ptr(20, 22);
    int r2 = global_func_ptr2(20, 22);
    if (r1 != 42) return 1;
    if (r2 != 42) return 2;
    return 42;
}
