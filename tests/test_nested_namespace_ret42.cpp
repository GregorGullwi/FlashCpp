// Test: Nested namespace support
// This test verifies that the compiler can:
// 1. Parse nested namespace declarations
// 2. Define functions inside nested namespaces
// 3. Generate correct code for nested namespaced functions

namespace A {
    namespace B {
        int func() {
            return 42;
        }
    }
}

int main() {
    int x = A::B::func(42);
    return x;  // Should return 42
}

