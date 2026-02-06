// Test: Function pointer parameters with pointer return types
// Verifies parsing of void *(*callback)(void *) pattern
typedef void *(*thread_func_t)(void *);
void start_thread(thread_func_t func, void *arg) {}
void launch(void *(*routine)(void *), void *data) {}

int main() {
	return 3;
}
