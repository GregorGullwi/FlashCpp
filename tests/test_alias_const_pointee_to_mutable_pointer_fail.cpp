using const_void_ptr = const void*;

int sink(void* ptr) {
	(void)ptr;
	return 0;
}

int main() {
	const_void_ptr ptr = (const void*)0;
	return sink(ptr);
}
