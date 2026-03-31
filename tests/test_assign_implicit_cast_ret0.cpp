// Test: assignment RHS implicit conversions via sema annotation (Phase 7).
// Validates that the semantic pass annotates assignment RHS expressions
// and codegen consumes those annotations instead of ad-hoc local policy.

int main() {
 // int -> long assignment
	long x = 0L;
	int a = 5;
	x = a;
	long r1 = x - 5L;  // should be 0

 // int -> double assignment
	double d = 0.0;
	d = a;
	int r2 = (int)(d - 5.0);	 // should be 0

 // float -> int assignment (truncation)
	float f = 3.7f;
	int i = 0;
	i = f;
	int r3 = i - 3;	// should be 0

 // double -> int assignment (truncation)
	double d2 = 7.9;
	int j = 0;
	j = d2;
	int r4 = j - 7;	// should be 0

	return (int)r1 + r2 + r3 + r4;
}
