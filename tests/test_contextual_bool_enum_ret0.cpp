// Phase 8b: Enum values used in boolean contexts (if/while/for/ternary)
// should receive implicit contextual-bool conversion annotations from sema.
// C++20 [conv.bool]: zero maps to false; any other value maps to true.

enum Color { Red = 0, Green = 1, Blue = 2 };
enum class Severity { None = 0, Warning = 1, Error = 2 };

int main() {
	Color c = Green;
	int result = 0;

	// Unscoped enum in if condition: Green (1) is truthy.
	if (c) {
		result += 1;
	}

	// Unscoped enum zero value: Red (0) is falsy.
	Color r = Red;
	if (r) {
		result += 100;  // should NOT execute
	}

	// Ternary condition with enum.
	result += c ? 10 : 0;

	// Expected: result == 11 (1 + 10)
	return result - 11;
}
