// Test that verifies type resolution and overload resolution
// for "long long int" vs "int" to ensure mangling is correct

void func(int x) {
	// Should not be called
	(void)x;
}

void func(long long int x) {
	// Should be called
	(void)x;
}

void func_unsigned(unsigned int x) {
	// Should not be called
	(void)x;
}

void func_unsigned(unsigned long long int x) {
	// Should be called
	(void)x;
}

int main() {
	// These should call the long long overloads
	long long int a = 42;
	unsigned long long int b = 42;
	
	func(a);
	func_unsigned(b);
	
	// Test that type resolution is correct
	if (sizeof(a) == 8 && sizeof(b) == 8) {
		return 42;
	}
	return 0;
}
