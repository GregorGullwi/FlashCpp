// One captureless generic lambda can convert to several distinct
// function-pointer signatures. Each target must produce its own operator() and
// __invoke instantiation with the deduced parameter and return types.

static int apply_int(int (*function)(int), int value) {
	return function(value);
}

static double apply_double(double (*function)(double), double value) {
	return function(value);
}

int main() {
	auto add_one = [](auto value) {
		return value + 1;
	};
	int (*int_function)(int) = add_one;
	double (*double_function)(double) = add_one;
	if (int_function(4) != 5 || apply_int(add_one, 5) != 6) {
		return 1;
	}
	if (double_function(2.5) != 3.5 || apply_double(add_one, 3.5) != 4.5) {
		return 2;
	}

	auto add_two = []<class T>(T value) {
		return value + 2;
	};
	int (*explicit_int)(int) = add_two;
	double (*explicit_double)(double) = add_two;
	if (explicit_int(4) != 6 || apply_int(add_two, 5) != 7) {
		return 3;
	}
	if (explicit_double(2.5) != 4.5 || apply_double(add_two, 3.5) != 5.5) {
		return 4;
	}
	return 0;
}
