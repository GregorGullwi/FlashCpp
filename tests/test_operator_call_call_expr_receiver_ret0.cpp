struct Adder {
	int base;

	int operator()(int value) const {
		return base + value;
	}
};

Adder makeAdder(int value) {
	Adder out;
	out.base = value;
	return out;
}

int main() {
	int result = makeAdder(40)(2);
	return result == 42 ? 0 : 1;
}
