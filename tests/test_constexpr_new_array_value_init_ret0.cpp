// Test: constexpr array new distinguishes bare default-initialization from value-initialization

constexpr int read_value_initialized_heap_array() {
	int* p = new int[3]{};
	int value = p[0] + p[1] + p[2];
	delete[] p;
	return value;
}

constexpr int write_then_read_default_initialized_heap_array() {
	int* p = new int[3];
	p[0] = 5;
	p[1] = 7;
	int value = p[0] + p[1];
	delete[] p;
	return value;
}

static_assert(read_value_initialized_heap_array() == 0);
static_assert(write_then_read_default_initialized_heap_array() == 12);

int main() { return 0; }
