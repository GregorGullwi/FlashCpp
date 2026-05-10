struct Box {
	int x;
	constexpr Box(int v) {
		int& r = x;
		r = v;
	}

	constexpr int get() const {
		return x;
	}
};

struct Counter {
	int value;

	constexpr Counter(int seed)
		: value(0) {
		int& ref = this->value;
		++ref;
		ref += seed;
	}
};

constexpr int localReferenceAlias() {
	int x = 3;
	int& r = x;
	r += 39;
	return x + r;
}

constexpr Box b(42);
constexpr Counter c(41);
static_assert(b.get() == 42);
static_assert(c.value == 42);
static_assert(localReferenceAlias() == 84);

int main() {
	return 0;
}
