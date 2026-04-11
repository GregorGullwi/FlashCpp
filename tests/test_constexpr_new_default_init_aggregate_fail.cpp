struct Pair {
	int x;
	int y;
};

constexpr int read_default_initialized_heap_aggregate() {
	Pair* p = new Pair;
	int value = p->x;
	delete p;
	return value;
}

static_assert(read_default_initialized_heap_aggregate() == 0);

int main() { return 0; }
