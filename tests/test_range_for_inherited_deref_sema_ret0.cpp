// Regression test: range-for sema should pre-resolve operator* even when the
// iterator inherits that dereference operator from a base class.

struct DerefBase {
	int operator*() {
		return 14;
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

int main() {
	Numbers numbers;
	int sum = 0;
	for (int value : numbers) {
		sum += value;
	}
	return sum == 42 ? 0 : 1;
}
