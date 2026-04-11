struct Adder {
	int base;

	constexpr Adder(int b = 10) : base(b) {}

	constexpr int add(int x) const {
		return base + x;
	}

	constexpr int scale(int x, int factor = 2) const {
		return base * factor + x;
	}
};

constexpr Adder g_default{};
constexpr Adder g_explicit{5};

static_assert(g_default.add(3) == 13);
static_assert(g_explicit.add(3) == 8);
static_assert(g_default.scale(1) == 21);
static_assert(g_explicit.scale(1, 3) == 16);

int main() {
	return (g_default.add(3) == 13
		&& g_explicit.add(3) == 8
		&& g_default.scale(1) == 21
		&& g_explicit.scale(1, 3) == 16) ? 0 : 1;
}
