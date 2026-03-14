// Test: void* reference initialization from temporaries and function returns.
// Exercises the is_likely_pointer heuristic in IRConverter_Conv_VarDecl.h
// to ensure void* values are correctly treated as pointer-like when
// initializing references.

void* get_void_ptr(int* p) {
    return static_cast<void*>(p);
}

int main() {
    int x = 42;
    int* ip = &x;

    // Test 1: void* variable, then bind reference
    void* vp = static_cast<void*>(ip);
    void*& ref1 = vp;
    int* back1 = static_cast<int*>(ref1);
    if (*back1 != 42) return 1;

    // Test 2: modify through void* reference
    int y = 99;
    ref1 = static_cast<void*>(&y);
    int* back2 = static_cast<int*>(vp);  // vp should now point to y
    if (*back2 != 99) return 2;

    // Test 3: const void* reference
    const void* cvp = static_cast<const void*>(ip);
    const void* const& cref = cvp;
    const int* back3 = static_cast<const int*>(cref);
    if (*back3 != 42) return 3;

    return 0;
}
