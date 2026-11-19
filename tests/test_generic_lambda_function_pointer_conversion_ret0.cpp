// A captureless generic lambda converts to a concrete function pointer by
// instantiating its call operator with the target parameter types. The
// resulting __invoke address must use those concrete types in every conversion
// context, including aggregate returns through a hidden return slot.

struct Large {
	long long value;
	long long extra;

	Large(int initial) : value(initial), extra(initial + 1) {}

	Large(const Large& other) : value(other.value), extra(other.extra) {}
};

static int apply(int (*function)(int, int), int left, int right) {
	return function(left, right);
}

static Large (*make_large_factory())(int) {
	return [](auto value) {
		return Large(value + 1);
	};
}

int main() {
	auto add = [](auto left, auto right) {
		return left + right;
	};

	int (*initialized)(int, int) = add;
	if (initialized(2, 3) != 5) {
		return 1;
	}

	int (*assigned)(int, int) = nullptr;
	assigned = add;
	if (assigned(4, 5) != 9) {
		return 2;
	}

	if (apply(add, 6, 7) != 13) {
		return 3;
	}

	auto read = [](auto const& value) {
		return value;
	};
	int (*reader)(const int&) = read;
	int input = 11;
	if (reader(input) != 11) {
		return 4;
	}

	Large (*factory)(int) = make_large_factory();
	Large result = factory(8);
	if (result.value != 9 || result.extra != 10) {
		return 5;
	}
	return 0;
}
