// Regression test for ExprResult migration (Phase 2):
// Global pointer variables must have metadata=0 in the 4th operand slot,
// NOT pointer_depth. The toTypedValue consumer decodes non-struct metadata
// as pointer_depth; a spurious pointer_depth=1 can corrupt downstream
// codegen (pointer arithmetic, dereference logic).

int val = 42;
int* g_ptr = &val;

int main() {
    return *g_ptr;
}
