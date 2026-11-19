// A captureless lambda's implicit conversion to pointer-to-function must produce
// the generated __invoke entry point in every conversion context, not a copy of
// the closure object.

extern "C" int printf(const char*, ...);

static int calls = 0;

struct Large {
	long long value;
	long long extra;

	Large(int initial) : value(initial), extra(initial) {}

	Large(const Large& other) : value(other.value), extra(other.extra) {}
};

static int apply(int (*function)(int), int value) {
	return function(value);
}

static int (*make_doubler())(int) {
	return [](int value) {
		++calls;
		return value * 2;
	};
}

static Large (*make_large())(int) {
	return [](int value) {
		++calls;
		return Large(value + 1);
	};
}

int main() {
	auto increment = [](int value) {
		++calls;
		return value + 1;
	};

	int (*initialized)(int) = increment;
	if (initialized(1) != 2) {
		return 1;
	}

	int (*assigned)(int) = nullptr;
	assigned = increment;
	if (assigned(4) != 5) {
		return 2;
	}

	if (apply(increment, 9) != 10) {
		return 3;
	}

	int (*casted)(int) = static_cast<int (*)(int)>(increment);
	if (casted(6) != 7) {
		return 4;
	}

	int (*plussed)(int) = +increment;
	if (plussed(7) != 8) {
		return 5;
	}

	int (*doubler)(int) = make_doubler();
	if (doubler(5) != 10) {
		return 6;
	}

	Large (*large_factory)(int) = make_large();
	Large result = large_factory(41);
	if (result.value != 42 || result.extra != 42) {
		return 7;
	}
	if (calls != 7) {
		return 8;
	}
	return 0;
}
