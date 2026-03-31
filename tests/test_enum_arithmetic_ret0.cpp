// Test: enum arithmetic lowered correctly to underlying integer operations.
// Exercises the IrType lowering path: enum values should use integer
// arithmetic on their underlying representation.
// Per C++20 [expr.arith.conv], unscoped enums undergo integer promotion.

enum Color : int { Red = 1,
				   Green = 2,
				   Blue = 3 };

int test_enum_plus_enum() {
	int c = Red + Green;	 // Should be 3
	return c == 3 ? 0 : 1;
}

int test_enum_minus_enum() {
	int c = Blue - Red;	// Should be 2
	return c == 2 ? 0 : 2;
}

int test_enum_multiply() {
	int c = Green * Blue;  // Should be 6
	return c == 6 ? 0 : 4;
}

int test_enum_mixed_arithmetic() {
 // Enum + int: enum promotes to int
	int c = Blue + 7;  // Should be 10
	return c == 10 ? 0 : 8;
}

int main() {
	int result = 0;
	result += test_enum_plus_enum();
	result += test_enum_minus_enum();
	result += test_enum_multiply();
	result += test_enum_mixed_arithmetic();
	return result;
}
