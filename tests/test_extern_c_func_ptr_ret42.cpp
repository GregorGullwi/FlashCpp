// Test that extern "C" linkage is properly forwarded to function pointer types.
// This exercises the parse_declarator -> parse_postfix_declarator linkage
// forwarding path (the (*fp)(params) pattern).

extern "C" {
    int add_c(int a, int b) {
        return a + b;
    }
}

// Declare a function pointer variable pointing to an extern "C" function.
// This exercises the parenthesized declarator path: int (*fp)(int, int)
// where linkage must be forwarded through parse_postfix_declarator.
int (*func_ptr)(int, int) = add_c;

int main() {
    // Call through the function pointer to verify linkage is correct
    int result = func_ptr(20, 22);
    return result;  // Should be 42
}
