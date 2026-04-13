// Test: scalar constexpr new with paren-init still uses normal conversion rules

constexpr int read_paren_initialized_heap_scalar() {
	int* p = new int(3.14);
	int value = *p;
	delete p;
	return value;
}

static_assert(read_paren_initialized_heap_scalar() == 3);

int main() {
	return read_paren_initialized_heap_scalar() == 3 ? 0 : 1;
}
