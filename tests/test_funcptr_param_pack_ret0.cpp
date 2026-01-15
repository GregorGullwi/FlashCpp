// Test: Function pointer parameters with pack expansion and noexcept specifier
// This tests the parsing of complex function pointer type declarations

// Test 1: Unnamed function pointer parameter with pack expansion
template<typename Ret, typename... Args>
void test_unnamed_funcptr_pack(Ret (*)(Args...)) {
}

// Test 2: Named function pointer parameter
void test_named_funcptr(void (*callback)(void*)) {
}

// Test 3: Function pointer with noexcept specifier
void test_funcptr_noexcept(void (*)() noexcept) {
}

// Test 4: Function pointer with noexcept(expr) specifier
template<bool NE>
void test_funcptr_noexcept_expr(void (*)() noexcept(NE)) {
}

int main() {
    return 0;
}
