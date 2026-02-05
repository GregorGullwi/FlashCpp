// Test parsing of "long long int" type
// This should resolve to Type::LongLong, not Type::Int

int main() {
	// Test various forms of long long with explicit int keyword
	long long int a = 42;
	unsigned long long int b = 42;
	
	// These should also work (already fixed)
	long long c = 42;
	unsigned long long d = 42;
	
	// Test that all values are correct
	if (a == 42 && b == 42 && c == 42 && d == 42) {
		return 42;
	}
	return 0;
}
