struct Counter {
	int value;

	constexpr Counter(int v) : value(v) {}

	constexpr int get() const {
		return value;
	}

	constexpr void add(int delta) {
		value += delta;
	}
};

constexpr Counter global_counter(40);
constexpr const Counter* global_counter_ptr = &global_counter;

constexpr int read_through_global_ptr() {
	return global_counter_ptr->get();
}

constexpr int mutate_through_local_ptr() {
	Counter counter(10);
	Counter* ptr = &counter;
	ptr->add(5);
	return ptr->get() + counter.get();
}

constexpr int mutate_through_heap_ptr() {
	Counter* ptr = new Counter(20);
	ptr->add(2);
	int value = ptr->get();
	delete ptr;
	return value;
}

static_assert(read_through_global_ptr() == 40);
static_assert(mutate_through_local_ptr() == 30);
static_assert(mutate_through_heap_ptr() == 22);

int main() {
	// Runtime sub-tests mirror the static_assert checks above
	// to verify the codegen path as well.

	// 1. Read through global pointer
	if (read_through_global_ptr() != 40) {
		return 1;  // Failed - global pointer read
	}

	// 2. Mutate through local pointer
	if (mutate_through_local_ptr() != 30) {
		return 2;  // Failed - local pointer mutation
	}

	// 3. Mutate through heap pointer
	if (mutate_through_heap_ptr() != 22) {
		return 3;  // Failed - heap pointer mutation
	}

	// 4. Inline local-pointer scenario (not wrapped in constexpr fn)
	Counter c(100);
	Counter* p = &c;
	p->add(25);
	if (p->get() != 125) {
		return 4;  // Failed - inline local pointer get
	}
	if (c.get() != 125) {
		return 5;  // Failed - mutation not visible on original object
	}

	// 6. Inline heap-pointer scenario
	Counter* hp = new Counter(50);
	hp->add(7);
	if (hp->get() != 57) {
		delete hp;
		return 6;  // Failed - inline heap pointer mutation
	}
	delete hp;

	return 0;  // Success
}
