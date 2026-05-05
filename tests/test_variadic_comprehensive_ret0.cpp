// Comprehensive test for variadic function templates

template <typename... Args>
struct Tuple {
};

extern "C" int declared_sum_ints(int count, ...);
extern "C" void declared_log_message(const char* format, int level, ...);

// Test 1: Empty parameter pack
template <typename... Args>
void empty_func(Args... args) {}

// Test 2: Parameter pack with different types
template <typename... Args>
int count_args(Args... args) {
	return sizeof...(Args);	// Should return number of arguments
}

// Test 3: Parameter pack with forwarding
template <typename... Args>
void forward_func(Args... args) {
	// Just accept and ignore the arguments
}

// Test 4: Mixed parameters - regular + pack
template <typename T, typename... Args>
T first_arg(T first, Args... rest) {
	return first;
}

int main() {
	Tuple<> empty_tuple;
	Tuple<int> single_tuple;
	Tuple<int, float, bool> triple_tuple;
	(void)empty_tuple;
	(void)single_tuple;
	(void)triple_tuple;

	// Test empty pack
	empty_func();

	// Test single argument
	empty_func(42);

	// Test multiple arguments
	empty_func(1, 2.5, 'a');

	// Test with many arguments
	forward_func(1, 2, 3, 4, 5, 6, 7, 8);

	// Test mixed parameters
	int x = first_arg(100, 200, 300);

	return 0;
}
