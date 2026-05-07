struct Pair {
	int value;
};

struct Defaults {
	int first = 40;
	int second = first + 1;
};

struct BraceDefaults {
	int first = 12;
	Pair pair{first};
};

struct BaseDefaults {
	int baseValue = 6;
};

struct DerivedDefaults : BaseDefaults {
	int extra = baseValue + 1;
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

constexpr void addBraceDefaults(BraceDefaults* p, int v) {
	p->pair.value += v;
}

constexpr void addDerivedDefaults(DerivedDefaults* p, int v) {
	p->baseValue += v;
	p->extra += p->baseValue;
}

struct Box {
	Pair pair;
	Defaults defaults;
	BraceDefaults braceDefaults;
	DerivedDefaults derivedDefaults;

	constexpr Box(int v) {
		pair.value = 0;
		setPair(&pair, v);
		addPair(&pair, 2);
		addDefaults(&defaults, 1);
		addBraceDefaults(&braceDefaults, 3);
		addDerivedDefaults(&derivedDefaults, 2);
	}
};

constexpr Box box(40);

static_assert(box.pair.value == 42);
static_assert(box.defaults.first == 40);
static_assert(box.defaults.second == 42);
static_assert(box.braceDefaults.first == 12);
static_assert(box.braceDefaults.pair.value == 15);
static_assert(box.derivedDefaults.baseValue == 8);
static_assert(box.derivedDefaults.extra == 15);

int main() {
	return box.pair.value == 42 &&
		box.defaults.first == 40 &&
		box.defaults.second == 42 &&
		box.braceDefaults.first == 12 &&
		box.braceDefaults.pair.value == 15 ? 0 : 1;
}
