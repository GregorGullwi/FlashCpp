// Test multi-statement constexpr lambda evaluation

constexpr auto sum_even = [](int n) {
	int sum = 0;
	for (int i = 1; i <= n; ++i) {
		if (i % 2 == 0) {
			sum += i;
		}
	}
	return sum;
};

static_assert(sum_even(10) == 30, "multi-statement constexpr lambda should sum even numbers");

constexpr auto make_forty_two = []() {
	int x = 40;
	int y = 2;
	return x + y;
};

static_assert(make_forty_two() == 42, "multi-statement constexpr lambda should support local variables");

struct Counter {
	int base;

	constexpr int sum_to(int n) const {
		int sum = base;
		for (int i = 1; i <= n; ++i) {
			sum += i;
		}
		return sum;
	}
};

constexpr Counter counter{2};
static_assert(counter.sum_to(4) == 12, "multi-statement constexpr member function should work");

int main() {
	return 0;
}