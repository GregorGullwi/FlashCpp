// Extended tests for default function arguments:
// - Member functions with defaults
// - Template functions with defaults
// - Multiple defaults with partial calls
// - Implicit this with defaults

struct Calculator {
	int offset;
	int compute(int a, int b = 10) {
		return a + b + offset;
	}
};

template <typename T>
T tmpl_add(T a, T b = 32) {
	return a + b;
}

int free_multi(int a, int b = 5, int c = 7) {
	return a + b + c;
}

struct Inner {
	int value;
	int get(int extra = 0) {
		return value + extra;
	}
};

int main() {
	// Member function with default arg
	Calculator c;
	c.offset = 22;
	int r1 = c.compute(10);			// 10 + 10 + 22 = 42
	int r2 = c.compute(10, 10);		// 10 + 10 + 22 = 42
	if (r1 != 42)
		return 1;
	if (r2 != 42)
		return 2;

	// Template function with default arg
	int r3 = tmpl_add<int>(10);		// 10 + 32 = 42
	int r4 = tmpl_add<int>(20, 22);	// 20 + 22 = 42
	if (r3 != 42)
		return 3;
	if (r4 != 42)
		return 4;

	// Free function with multiple defaults
	int r5 = free_multi(30);			 // 30 + 5 + 7 = 42
	int r6 = free_multi(30, 5);		// 30 + 5 + 7 = 42
	int r7 = free_multi(30, 5, 7);	   // 30 + 5 + 7 = 42
	if (r5 != 42)
		return 5;
	if (r6 != 42)
		return 6;
	if (r7 != 42)
		return 7;

	// Member function with zero-arg default
	Inner inner;
	inner.value = 42;
	int r8 = inner.get();			  // 42 + 0 = 42
	int r9 = inner.get(0);		   // 42 + 0 = 42
	if (r8 != 42)
		return 8;
	if (r9 != 42)
		return 9;

	return 42;
}
