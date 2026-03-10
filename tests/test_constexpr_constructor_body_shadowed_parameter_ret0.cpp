struct InitOnlyShadow {
	int x;

	constexpr InitOnlyShadow(int x)
		: x(x * 2) {}
};

struct BodyShadow {
	int x;

	constexpr BodyShadow(int x)
		: x(x * 2) {
		this->x = x + 1;
	}
};

struct LocalShadow {
	int x;

	constexpr LocalShadow(int value)
		: x(value) {
		int x = 7;
		this->x = x + 1;
	}
};

constexpr InitOnlyShadow init_only{5};
constexpr BodyShadow body_shadow{5};
constexpr LocalShadow local_shadow{5};

static_assert(init_only.x == 10);
static_assert(body_shadow.x == 6);
static_assert(local_shadow.x == 8);

int main() {
	return 0;
}