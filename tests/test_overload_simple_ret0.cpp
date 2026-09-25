// Verify that an lvalue selects the lvalue-reference overload.

extern "C" int printf(const char*, ...);

// Test function overloads
void test_func(int& x) {
	printf("lvalue\n");
}

void test_func(int&& x) {
	printf("rvalue\n");
}

int main() {
	int a = 5;
	test_func(a);  // lvalue

	return 0;
}
