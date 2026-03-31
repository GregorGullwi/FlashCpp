// Phase 12: Enum→primitive sema annotation
// Tests enum→int conversion in return, assignment, call-arg, and declaration init contexts.

enum Color { Red = 0,
			 Green = 1,
			 Blue = 2 };

int identity(int x) { return x; }

int return_enum() {
	return Green;  // enum→int return conversion
}

int main() {
 // Test 1: return conversion (enum→int)
	int r1 = return_enum();
	if (r1 != 1)
		return 1;

 // Test 2: declaration init (enum→int)
	int r2 = Blue;
	if (r2 != 2)
		return 2;

 // Test 3: assignment RHS (enum→int)
	int r3;
	r3 = Red;
	if (r3 != 0)
		return 3;

 // Test 4: call arg (enum→int)
	int r4 = identity(Green);
	if (r4 != 1)
		return 4;

	return 0;
}
