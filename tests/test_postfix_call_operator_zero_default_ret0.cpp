// Regression: the shared postfix-call finalization path must resolve zero-arg
// and default-arg operator() overloads for cast-expression receivers rather
// than falling back to placeholder member calls.

struct ZeroOrOne {
	int bias;

	int operator()() const {
		return bias + 40;
	}

	int operator()(int value) const {
		return bias + value;
	}
};

struct Defaulted {
	int base;

	int operator()(int value = 7) const {
		return base + value;
	}
};

int main() {
	ZeroOrOne chooser{2};
	if (static_cast<ZeroOrOne&&>(chooser)() != 42) {
		return 1;
	}
	if (static_cast<ZeroOrOne&&>(chooser)(5) != 7) {
		return 2;
	}

	Defaulted def{35};
	if (static_cast<Defaulted&&>(def)() != 42) {
		return 3;
	}
	if (static_cast<Defaulted&&>(def)(7) != 42) {
		return 4;
	}

	return 0;
}
