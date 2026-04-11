// Test: reading a scalar allocated with bare `new T` is not a core constant expression

constexpr int read_default_initialized_heap_scalar() {
	int* p = new int;
	int value = *p;
	delete p;
	return value;
}

static_assert(read_default_initialized_heap_scalar() == 0);

int main() { return 0; }
