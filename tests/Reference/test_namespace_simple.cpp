// Test: Basic namespace support with function definition
// This test verifies that the compiler can:
// 1. Parse namespace declarations
// 2. Define functions inside namespaces
// 3. Generate correct code for namespaced functions

namespace std {
    int print(int value) {
        return value;
    }
}

int main() {
    return 0;
}

