// Regression: explicit member operator() syntax must treat defaulted
// parameters as viable during receiver-member recovery.

struct Callable {
	int operator()(int value = 42) const {
		return value;
	}

	long operator()(long value, long extra = 1) const {
		return value + extra + 1000;
	}
};

int main() {
	Callable callable;
	return callable.operator()();
}
