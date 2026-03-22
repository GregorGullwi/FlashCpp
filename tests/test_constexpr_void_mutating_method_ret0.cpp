// Tests for void mutating member function calls on local constexpr structs.

struct Counter {
	int value;
	constexpr Counter() : value(0) {}
	constexpr void increment() { value++; }
	constexpr void add(int n) { value += n; }
	constexpr int get() const { return value; }
};

// Basic void mutating call
constexpr int test_single_increment() {
	Counter c;
	c.increment();
	return c.get();
}
static_assert(test_single_increment() == 1);

// Multiple void mutating calls accumulate correctly
constexpr int test_multiple_increments() {
	Counter c;
	c.increment();
	c.increment();
	c.increment();
	return c.get();
}
static_assert(test_multiple_increments() == 3);

// Void method with parameter
constexpr int test_add() {
	Counter c;
	c.add(5);
	c.add(3);
	return c.get();
}
static_assert(test_add() == 8);

// Mix of void mutating and non-mutating calls
constexpr int test_mix() {
	Counter a;
	Counter b;
	a.increment();
	b.add(10);
	a.increment();
	return a.get() + b.get();
}
static_assert(test_mix() == 12);

// Void method in a loop
constexpr int test_loop() {
	Counter c;
	for (int i = 0; i < 7; ++i)
		c.increment();
	return c.get();
}
static_assert(test_loop() == 7);

int main() { return 0; }
