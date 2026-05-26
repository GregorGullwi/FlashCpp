// Regression: postfix callable syntax on a temporary receiver should resolve
// the concrete operator() overload in the parser/sema pipeline instead of
// relying on later receiver-member recovery from a placeholder call target.

struct OverloadedAdder {
	int base;

	int operator()(int value) const {
		return base + value;
	}

	int operator()(double value) const {
		return base + static_cast<int>(value) + 100;
	}
};

OverloadedAdder makeAdder(int base) {
	return OverloadedAdder{base};
}

int main() {
	if (makeAdder(40)(2) != 42) {
		return 1;
	}
	if (makeAdder(1)(2.0) != 103) {
		return 2;
	}
	return 0;
}
