// Test: reading an array element allocated with bare `new T[n]` is not a core constant expression

constexpr int read_default_initialized_heap_array() {
	int* p = new int[3];
	int value = p[1];
	delete[] p;
	return value;
}

static_assert(read_default_initialized_heap_array() == 0);

int main() { return 0; }
