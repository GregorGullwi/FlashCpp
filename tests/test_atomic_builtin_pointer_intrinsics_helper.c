int* flashcpp_atomic_add_fetch(int** ptr, unsigned long long value, int memory_order) __asm__("__atomic_add_fetch");
int* flashcpp_atomic_add_fetch(int** ptr, unsigned long long value, int memory_order) {
	(void)memory_order;
	*ptr = (int*)((char*)(*ptr) + value);
	return *ptr;
}

int* flashcpp_atomic_fetch_sub(int** ptr, unsigned long long value, int memory_order) __asm__("__atomic_fetch_sub");
int* flashcpp_atomic_fetch_sub(int** ptr, unsigned long long value, int memory_order) {
	(void)memory_order;
	int* old = *ptr;
	*ptr = (int*)((char*)(*ptr) - value);
	return old;
}
