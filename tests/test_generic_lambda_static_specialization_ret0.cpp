// Static locals in a generic lambda belong to the call operator
// specialization, not to the generated function that lowers it. The call
// operator and its __invoke thunk for one specialization share a counter, while
// the int and double specializations each own a separate counter.

static int through_int(int (*function)(int), int value) {
	return function(value);
}

static int through_double(int (*function)(double), double value) {
	return function(value);
}

int main() {
	auto counter = [](auto value) {
		static int count = 0;
		++count;
		return count;
	};

	if (counter(0) != 1) {
		return 1;
	}
	int (*int_function)(int) = counter;
	// __invoke for the int specialization shares operator()'s static.
	if (int_function(0) != 2) {
		return 2;
	}
	if (through_int(int_function, 0) != 3) {
		return 3;
	}

	// The double specialization must not reuse the int counter.
	if (counter(0.0) != 1) {
		return 4;
	}
	int (*double_function)(double) = counter;
	if (double_function(0.0) != 2) {
		return 5;
	}
	if (through_double(double_function, 0.0) != 3) {
		return 6;
	}
	return 0;
}
