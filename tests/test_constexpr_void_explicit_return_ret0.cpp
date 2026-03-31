// Tests for void constexpr member functions with explicit return; statements.

struct Counter {
	int value;
	constexpr Counter() : value(0) {}
	constexpr void reset() {
		value = 0;
		return;
	}
	constexpr void set(int v) {
		if (v < 0)
			return;
		value = v;
	}
	constexpr int get() const { return value; }
};

constexpr int test_trailing_return() {
	Counter c;
	c.set(5);
	c.reset();
	return c.get();
}
static_assert(test_trailing_return() == 0);

constexpr int test_early_exit() {
	Counter c;
	c.set(10);
	c.set(-1);
	return c.get();
}
static_assert(test_early_exit() == 10);

int main() { return 0; }
