// Test: constructor body with implicit arithmetic conversions
// Validates that sema normalizes constructor bodies and
// codegen's Phase 15/16 hard enforcement covers them.
struct Converter {
	int result;
	Converter(short s) {
	// implicit short -> int promotion in constructor body
		result = s + 100;
	}
};

int main() {
	Converter c(42);
	return c.result - 142;  // 42 + 100 = 142, so returns 0
}
