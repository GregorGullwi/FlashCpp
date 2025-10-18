// Test: Basic namespace support with function definition
// This test verifies that the compiler can:
// 1. Parse namespace declarations
// 2. Define functions inside namespaces
// 3. Generate correct code for namespaced functions

namespace A {
    int print(int value) {
        return value;
    }

    namespace B {
        int func() {
            return 42;
        }
    }
}

int main() {
    return 0;
}

