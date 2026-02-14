// Test that calling a function in an undeclared namespace is rejected.
// f2 namespace does not exist, so f2::func() should be an error.

namespace f {
    int func() { return 42; }
}

int main() {
    return f2::func();
}
