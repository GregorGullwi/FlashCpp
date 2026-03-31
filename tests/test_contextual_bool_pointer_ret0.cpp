// Phase 8c: Pointer values used in boolean contexts (if/while/for/ternary)
// should receive implicit contextual-bool conversion annotations from sema.
// C++20 [conv.bool]: null pointer maps to false; non-null maps to true.

int main() {
	int x = 42;
	int* p = &x;
	int* q = nullptr;
	int result = 0;

 // Non-null pointer in if condition: truthy.
	if (p) {
		result += 1;
	}

 // Null pointer in if condition: falsy.
	if (q) {
		result += 100;  // should NOT execute
	}

 // Ternary condition with pointer.
	result += p ? 10 : 0;

 // Expected: result == 11 (1 + 10)
	return result - 11;
}
