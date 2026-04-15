// Regression test: range-for sema should pre-resolve operator* even when the
// iterator inherits that dereference operator from a base class.

struct DerefBase {
	int operator*() {
		return 14;
	}

	int operator*() const {
		return 21;
	}
};

struct Iter : DerefBase {
	int index;

	Iter& operator++() {
		++index;
		return *this;
	}

	bool operator!=(Iter other) const {
		return index != other.index;
	}
};

struct Numbers {
	Iter begin() {
		Iter it;
		it.index = 0;
		return it;
	}

	Iter end() {
		Iter it;
		it.index = 3;
		return it;
	}
};

struct ConstNumbers {
	Iter begin() const {
		Iter it;
		it.index = 0;
		return it;
	}

	Iter end() const {
		Iter it;
		it.index = 2;
		return it;
	}
};

int main() {
	Numbers numbers;
	int sum = 0;
	for (int value : numbers) {
		sum += value;
	}

	const ConstNumbers const_numbers;
	int const_sum = 0;
	for (int value : const_numbers) {
		const_sum += value;
	}

	if (sum != 42)
		return 1;
	if (const_sum != 28)
		return 2;
	return 0;
}
