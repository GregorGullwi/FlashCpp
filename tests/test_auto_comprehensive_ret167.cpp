// Comprehensive auto type deduction test
// Tests various auto features to document what works

struct Point {
	int x;
	int y;
};

Point makePoint(int x, int y) {
	Point p;
	p.x = x;
	p.y = y;
	return p;
}

int testAutoExpression() {
	auto x = 10 + 20;
	return x;
}

int testAutoFromVariable() {
	int y = 100;
	auto x = y;
	return x;
}

int main() {
	// Test 1: auto with literals - SHOULD WORK
	auto a = 42;

	// Test 2: auto with expressions - SHOULD WORK
	auto b = a + 10;

	// Test 3: auto with function return - SHOULD WORK
	auto p = makePoint(5, 10);

	// Test 4: auto reference - TEST THIS (simplified - no mutation)
	int x = 50;
	const auto& cref = x;  // const reference works with lvalues

	// Test 5: const auto - TEST THIS
	const auto c = 25;

	// Test 6: auto* pointer - TEST THIS
	int y = 35;
	int* ptr = &y;
	auto* auto_ptr = ptr;

	if (testAutoExpression() != 30)
		return 1;
	if (testAutoFromVariable() != 100)
		return 2;

	// Return sum to verify all worked
	return a + cref + c + *auto_ptr + p.x + p.y;
	// = 42 + 50 + 25 + 35 + 5 + 10 = 167
}
