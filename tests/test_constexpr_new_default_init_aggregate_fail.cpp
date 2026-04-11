struct Mixed {
	int x = 42;
	double y;
};

constexpr int read_default_initialized_heap_aggregate() {
	Mixed* p = new Mixed;
	double value = p->y;
	delete p;
	return value == 0.0 ? 0 : 1;
}

static_assert(read_default_initialized_heap_aggregate() == 0);

int main() { return 0; }
