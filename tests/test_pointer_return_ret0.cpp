// Regression test: pointer arithmetic result must retain pointer_depth so that
// downstream subscript and dereference emit the correct 64-bit load.
//
// Bug 1 (IrGenerator_Expr_Operators): p+1 was emitted with PointerDepth{0}
//   instead of PointerDepth{1}, causing the backend to treat the result as an
//   8-bit char value rather than a 64-bit pointer.
//
// Bug 2 (IrGenerator_Call_Direct): array-to-pointer decay fallback was gated
//   on !sema_normalized_current_function_, so local arrays (const char s[])
//   were passed by value (8-bit char) instead of by address (64-bit pointer).
//
// Note: local array initialisation from string literals (const char s[] = "str")
// is a known pre-existing limitation; use brace-initialisation here instead.
//
// Expected exit code: 0

const char* pick(const char* a, bool choose_first) {
    if (choose_first)
        return a;
    return a + 1;
}

int main() {
    // Brace-initialised so that the array is properly set up on the stack.
    const char s[] = {'*', 'h', 'i', 'd', 'd', 'e', 'n', 0};
    // pick(s, false) returns s+1 (pointer to 'h')
    // r[0] must dereference that pointer and return 'h' (104)
    const char* r = pick(s, false);
    // If pointer arithmetic loses pointer_depth, r holds 43 (ASCII '*'+1)
    // and r[0] would segfault.
    if (r[0] != 'h')
        return 1;
    return 0;
}
