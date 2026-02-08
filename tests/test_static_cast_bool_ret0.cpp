// Test static_cast<bool> normalizes integer values to 0 or 1

int main() {
	// Test 1: non-zero integer normalizes to true (1)
	bool b1 = static_cast<bool>(42);
	if (b1 != true) return 1;

	// Test 2: zero normalizes to false (0)
	bool b2 = static_cast<bool>(0);
	if (b2 != false) return 2;

	// Test 3: bool value usable in logical AND with comparisons
	bool cond = (1 == 1) && (2 == 2) && b1;
	if (!cond) return 3;

	// Test 4: C-style cast also normalizes
	bool b3 = (bool)100;
	if (b3 != true) return 4;

	// Test 5: negative number normalizes to true
	bool b4 = static_cast<bool>(-5);
	if (b4 != true) return 5;

	// Test 6: char value normalizes
	char c = 65;  // 'A'
	bool b5 = static_cast<bool>(c);
	if (b5 != true) return 6;

	return 0;
}
