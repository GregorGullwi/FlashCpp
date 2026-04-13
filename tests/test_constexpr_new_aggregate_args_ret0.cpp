struct Pair {
	int x;
	int y;
};

constexpr int read_heap_aggregate_brace_args() {
	Pair* p = new Pair{3, 4};
	int value = p->x + p->y;
	delete p;
	return value;
}

constexpr int read_heap_aggregate_paren_args() {
	Pair* p = new Pair(5, 6);
	int value = p->x + p->y;
	delete p;
	return value;
}

static_assert(read_heap_aggregate_brace_args() == 7);
static_assert(read_heap_aggregate_paren_args() == 11);

int main() {
	return (read_heap_aggregate_brace_args() == 7 &&
			read_heap_aggregate_paren_args() == 11)
		? 0
		: 1;
}
