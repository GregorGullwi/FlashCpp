// Tests for default constructor body invocation on uninitialized local struct variables.

struct Simple {
	int value;
	constexpr Simple() {
		value = 42;
	}
};

static_assert([] {
	Simple s;
	return s.value;
}() == 42);

// Constructor body sets multiple members
struct TwoField {
	int x;
	int y;
	constexpr TwoField() {
		x = 10;
		y = 20;
	}
};

constexpr int test_two_field() {
	TwoField t;
	return t.x + t.y;
}
static_assert(test_two_field() == 30);

// Constructor body with computation
struct Computed {
	int a;
	int b;
	int sum;
	constexpr Computed() {
		a = 3;
		b = 4;
		sum = a + b;
	}
};

constexpr int test_computed() {
	Computed c;
	return c.sum;
}
static_assert(test_computed() == 7);

// Mix of init-list and body-assigned members still works
struct Mixed {
	int value;
	int step;
	constexpr Mixed() : value(1) {
		step = 2;
	}
};

constexpr int test_mixed() {
	Mixed m;
	return m.value + m.step;
}
static_assert(test_mixed() == 3);

// Two separate locals both get default-constructed independently
constexpr int test_two_locals() {
	Simple a;
	Simple b;
	return a.value + b.value;
}
static_assert(test_two_locals() == 84);

int main() { return 0; }
