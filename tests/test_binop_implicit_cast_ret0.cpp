// Test: binary operator usual arithmetic conversions (Phase 2 sema migration).
// The semantic pass annotates operands with their common-type conversions.

int main() {
	// int + long → common type is long, result = 7
	int i = 3;
	long l = 4L;
	long r1 = i + l;

	// int * unsigned int → common type is unsigned int, result = 30
	unsigned int u = 10u;
	unsigned int r2 = (unsigned int)(i * u);

	// Same-type: int - int = int, no conversion needed
	int r3 = i - i;

	return (int)(r1 - 7L) + (int)(r2 - 30u) + r3;
}
