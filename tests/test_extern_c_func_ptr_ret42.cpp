// Test that extern "C" linkage is properly forwarded to function pointer types
extern "C" {
    int add_c(int a, int b) {
        return a + b;
    }
}

int main() {
    int result = add_c(20, 22);
    return result;  // Should be 42
}
