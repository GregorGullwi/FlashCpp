// This test verifies that forming a pointer-to-reference in a trailing return type
// during template instantiation produces a clean compile error (not a crash).
// The try-catch in resolveMaterializedTrailingReturnType catches the CompileError
// from consume_pointer_ref_modifiers and routes it through failTemplateInstantiation.
// Without the try-catch, T&* in the trailing return would throw an uncaught exception.

template<typename T>
auto f() -> T* { return nullptr; }

struct X {};
int main() {
    // int&* and X&* are both pointer-to-reference — invalid C++.
    // The compiler should report a clean error, not crash.
    auto p = f<X&>();
    (void)p;
    return 0;
}
