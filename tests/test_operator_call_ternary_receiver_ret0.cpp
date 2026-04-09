struct Adder {
	int base;

	int operator()(int value) {
		return base + value;
	}
};

int main() {
	Adder left;
	Adder right;
	left.base = 10;
	right.base = 40;

	bool pick_left = false;
	int result = (pick_left ? left : right)(2);
	return result == 42 ? 0 : 1;
}
