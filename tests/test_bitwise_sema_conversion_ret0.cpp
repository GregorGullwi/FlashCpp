// Test: bitwise operators with implicit arithmetic conversions
// Validates that sema annotates usual arithmetic conversions for &, |, ^.
int main() {
	short a = 5;
	int b = 3;
	// C++20 [expr.bit.and]: usual arithmetic conversions promote short to int
	int c = a & b;  // 5 & 3 = 1
	int d = a | b;  // 5 | 3 = 7
	int e = a ^ b;  // 5 ^ 3 = 6
	return c + d + e - 14;  // 1 + 7 + 6 = 14, returns 0
}
