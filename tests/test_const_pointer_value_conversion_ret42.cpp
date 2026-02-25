// Regression: passing a const-qualified pointer object (T* const) by value
// must not be treated as converting const T* -> T*.

void sink(void* dst, int value, unsigned long long size) {
	(void)dst;
	(void)value;
	(void)size;
}

int main() {
	char buffer[8] = {0};
	void* const destination = buffer;
	unsigned long long bytes = 8;
	sink(destination, 0, bytes);
	return 42;
}
