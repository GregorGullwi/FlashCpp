// Test enum lvalue assignment through array elements and pointer dereference
// This exercises the handleLValueAssignment codegen path for enum values.

enum Color : int { Red = 1,
				   Green = 2,
				   Blue = 3 };

int main() {
 // Test enum array element assignment
	Color colors[3];
	colors[0] = Red;
	colors[1] = Green;
	colors[2] = Blue;
	if (colors[0] != Red)
		return 1;
	if (colors[1] != Green)
		return 2;
	if (colors[2] != Blue)
		return 3;

 // Test enum through pointer dereference assignment
	Color c = Red;
	Color* p = &c;
	*p = Blue;
	if (c != Blue)
		return 4;

 // Test enum pointer arithmetic correctness (stride = sizeof(Color) = 4)
	Color* p2 = colors;
	if (*p2 != Red)
		return 5;
	++p2;
	if (*p2 != Green)
		return 6;
	++p2;
	if (*p2 != Blue)
		return 7;

	return 0;
}
