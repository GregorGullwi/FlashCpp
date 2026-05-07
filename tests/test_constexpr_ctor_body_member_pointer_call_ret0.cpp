struct Pair {
	int value;
};

constexpr void set_pair(Pair* p, int v) {
	p->value = v;
}

constexpr void add_pair(Pair* p, int v) {
	p->value += v;
}

struct Box {
	Pair pair;

	constexpr Box(int v) {
		pair.value = 0;
		set_pair(&pair, v);
		add_pair(&pair, 2);
	}
};

constexpr Box box(40);

static_assert(box.pair.value == 42);

int main() {
	return box.pair.value == 42 ? 0 : 1;
}
