// Phase 10: Local variable simple assignment sema annotation consumption.
// Validates that the codegen path for `x = expr` prefers sema annotations
// over local policy, consistent with binary operator and global assignment paths.
// Tests multiple cross-type assignments: int→double, double→int, char→int,
// int→float, long→int.

int main() {
	int result = 0;

 // int → double assignment
	double d = 0.0;
	int a = 42;
	d = a;
	if (d > 41.5)
		result += 1;

 // double → int assignment (truncation)
	int b = 0;
	double d2 = 7.9;
	b = d2;
	if (b == 7)
		result += 10;

 // char → int assignment
	int c = 0;
	char ch = 65;
	c = ch;
	if (c == 65)
		result += 100;

 // int → float assignment
	float f = 0.0f;
	int e = 99;
	f = e;
	if (f > 98.5f)
		result += 1000;

 // long → int assignment (narrowing)
	int g = 0;
	long lg = 77L;
	g = lg;
	if (g == 77)
		result += 10000;

 // Expected: 11111
	return result - 11111;
}
