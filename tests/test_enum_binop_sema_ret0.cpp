// Phase 12: Enum in binary operations with sema annotation
// Tests enum operands in arithmetic and comparison expressions.

enum Color { Red = 0,
			 Green = 1,
			 Blue = 2 };

int main() {
 // Test 1: enum + int → int (enum promoted to int)
	int r1 = Green + 10;
	if (r1 != 11)
		return 1;

 // Test 2: int + enum → int (enum promoted to int)
	int r2 = 10 + Blue;
	if (r2 != 12)
		return 2;

 // Test 3: enum comparison with int
	Color c = Green;
	if (c != 1)
		return 3;

 // Test 4: enum - enum → int
	int diff = Blue - Green;
	if (diff != 1)
		return 4;

	return 0;
}
