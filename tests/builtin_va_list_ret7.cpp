// Test: __builtin_va_list as a recognized type
void test_func(int n, ...) {
	__builtin_va_list args;
	__builtin_va_start(args, n);
	__builtin_va_end(args);
}

int main() {
	return 7;
}
