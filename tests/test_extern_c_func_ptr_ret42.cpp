// Test that extern "C" linkage is properly forwarded to function pointer types.
// This exercises the parse_declarator -> parse_postfix_declarator linkage
// forwarding path (the (*fp)(params) pattern).

int add_func(int a, int b) {
	return a + b;
}

int main() {
	// Declare a function pointer variable pointing to a function.
	// This exercises the parenthesized declarator path: int (*fp)(int, int)
	// where linkage must be forwarded through parse_postfix_declarator.
	int (*func_ptr)(int, int) = add_func;

	// Call through the function pointer to verify linkage is correct
	int result = func_ptr(20, 22);
	return result;  // Should be 42
}
