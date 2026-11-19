// An explicit lambda template parameter can appear in a trailing return type.
// Conversion deduction must substitute the deduced argument into that return
// type before comparing it with the function-pointer target.

static int apply_int(int (*function)(int), int value) {
	return function(value);
}

static double apply_double(double (*function)(double), double value) {
	return function(value);
}

int main() {
	auto identity = []<class T>(T value) -> T {
		return value;
	};
	if (apply_int(identity, 42) != 42) {
		return 1;
	}
	if (apply_double(identity, 2.5) != 2.5) {
		return 2;
	}
	return 0;
}
