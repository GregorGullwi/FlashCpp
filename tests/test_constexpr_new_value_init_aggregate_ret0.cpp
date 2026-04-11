struct Pair {
	int x;
	int y;
};

constexpr int read_value_initialized_heap_aggregate_paren() {
	Pair* p = new Pair();
	int value = p->x + p->y;
	delete p;
	return value;
}

constexpr int read_value_initialized_heap_aggregate_brace() {
	Pair* p = new Pair{};
	int value = p->x + p->y;
	delete p;
	return value;
}

struct WithDefault {
	int x = 42;
	int y;
};

constexpr int read_default_member_initializer_from_heap_aggregate() {
	WithDefault* p = new WithDefault;
	int value = p->x;
	delete p;
	return value;
}

static_assert(read_value_initialized_heap_aggregate_paren() == 0);
static_assert(read_value_initialized_heap_aggregate_brace() == 0);
static_assert(read_default_member_initializer_from_heap_aggregate() == 42);

int main() { return 0; }
