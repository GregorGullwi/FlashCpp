struct Pair {
	int value;
};

struct Defaults {
	int first = 40;
	int second = first + 1;
};

constexpr void setPair(Pair* p, int v) {
	p->value = v;
}

constexpr void addPair(Pair* p, int v) {
	p->value += v;
}

constexpr void addDefaults(Defaults* p, int v) {
	p->second += v;
}

struct Box {
	Pair pair;
	Defaults defaults;

	constexpr Box(int v) {
		pair.value = 0;
		setPair(&pair, v);
		addPair(&pair, 2);
		addDefaults(&defaults, 1);
	}
};

constexpr Box box(40);

static_assert(box.pair.value == 42);
static_assert(box.defaults.first == 40);
static_assert(box.defaults.second == 42);

int main() {
	return box.pair.value == 42 &&
		box.defaults.first == 40 &&
		box.defaults.second == 42 ? 0 : 1;
}
