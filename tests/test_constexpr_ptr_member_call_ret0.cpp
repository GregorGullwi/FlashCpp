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
	return 0;
}
