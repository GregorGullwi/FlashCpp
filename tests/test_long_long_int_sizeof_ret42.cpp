// Test that verifies sizeof(long long int) == 8 (64 bits)
// If the parser incorrectly treats "long long int" as "int", 
// this would be 4 bytes instead of 8 bytes

int main() {
	long long int a = 0;
	unsigned long long int b = 0;
	
	// sizeof(long long) should be 8, sizeof(int) should be 4
	// This test verifies the types are being correctly parsed
	int size_a = sizeof(a);
	int size_b = sizeof(b);
	
	// Both should be 8 bytes (64 bits)
	if (size_a == 8 && size_b == 8) {
		return 42;
	}
	
	// Return different values to indicate which failed
	if (size_a != 8) {
		return 1; // long long int parsed incorrectly
	}
	if (size_b != 8) {
		return 2; // unsigned long long int parsed incorrectly
	}
	
	return 0;
}
