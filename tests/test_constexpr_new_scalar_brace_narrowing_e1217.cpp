// Test: builtin direct-list-initialization rules apply to scalar constexpr new

constexpr int read_narrowed_heap_scalar() {
	int* p = new int{3.14};
	int value = *p;
	delete p;
	return value;
}

static_assert(read_narrowed_heap_scalar() == 3);

int main() { return 0; }
