// Test: nested struct default arguments via braced-init-list
// Uses Inner (8 bytes, fits in 1 register) to avoid the pre-existing
// 9-16 byte two-register ABI limitation.
// Exercises generateDefaultStructArg with nested InitializerListNode elements.

struct Inner {
	int a;
	int b;
};

// Default: Inner{20, 22} => a+b = 42
int sumInner(Inner i = {20, 22}) {
	return i.a + i.b;
}

// Multiple defaults: the second struct arg defaults to {5, 7}
// 30 + 5 + 7 = 42
int addInner(int x, Inner i = {5, 7}) {
	return x + i.a + i.b;
}

// Member function with struct default arg
struct Compute {
	int base;
	int run(Inner delta = {10, 20}) {
		return base + delta.a + delta.b;
	}
};

int main() {
	// 1. Use nested-struct default argument: Inner{20, 22}
	int r1 = sumInner();				 // 20 + 22 = 42
	if (r1 != 42)
		return 1;

	// 2. Override with explicit value
	Inner i2;
	i2.a = 2;
	i2.b = 40;
	int r2 = sumInner(i2);			   // 2 + 40 = 42
	if (r2 != 42)
		return 2;

	// 3. addInner with struct default
	int r3 = addInner(30);			   // 30 + 5 + 7 = 42
	if (r3 != 42)
		return 3;

	// 4. addInner with explicit struct
	Inner i4;
	i4.a = 2;
	i4.b = 10;
	int r4 = addInner(30, i4);		   // 30 + 2 + 10 = 42
	if (r4 != 42)
		return 4;

	// 5. Member function with struct default
	Compute c;
	c.base = 12;
	int r5 = c.run();				  // 12 + 10 + 20 = 42
	if (r5 != 42)
		return 5;

	return 42;
}
