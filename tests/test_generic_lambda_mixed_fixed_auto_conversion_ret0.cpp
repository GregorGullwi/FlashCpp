// A generic lambda can mix fixed and invented parameters. Only the invented
// parameters participate in specialization identity, so a direct call and a
// function-pointer conversion with the same deduced arguments must reuse one
// operator()/__invoke specialization.

static int apply_int(int (*function)(int, int), int base, int value) {
	return function(base, value);
}

static int apply_double(int (*function)(int, double), int base, double value) {
	return function(base, value);
}

int main() {
	auto add = [](int base, auto value) {
		return base + static_cast<int>(value);
	};
	if (add(1, 2) != 3) {
		return 1;
	}
	if (apply_int(add, 3, 4) != 7) {
		return 2;
	}
	if (apply_double(add, 5, 2.5) != 7) {
		return 3;
	}
	return 0;
}
