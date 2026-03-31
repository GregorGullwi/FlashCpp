// Phase 12: Enum→floating-point sema annotation
// Tests enum→double conversion in declaration init and call-arg contexts.

enum Size { Small = 1,
			Medium = 2,
			Large = 3 };

double to_double(double d) { return d; }

double return_enum_as_double() {
	return Medium;  // enum→double return conversion
}

int main() {
 // Test 1: enum→double declaration init
	double d1 = Medium;
	if (d1 < 1.9 || d1 > 2.1)
		return 1;

 // Test 2: enum→double call arg
	double d2 = to_double(Large);
	if (d2 < 2.9 || d2 > 3.1)
		return 2;

 // Test 3: enum→double return
	double d3 = return_enum_as_double();
	if (d3 < 1.9 || d3 > 2.1)
		return 3;

	return 0;
}
