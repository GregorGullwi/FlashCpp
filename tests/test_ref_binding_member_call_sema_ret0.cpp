struct Checker {
	int storage = 0;

	int takeLRef(int&) {
		return 1;
	}

	int takeRRef(int&&) {
		return 2;
	}

	int takeConstDouble(const double& value) {
		return value == 3.0 ? 3 : 30;
	}

	int takeValue(int value) {
		return value == 7 ? 4 : 40;
	}
};

int main() {
	Checker checker;
	int x = 5;
	if (checker.takeLRef(x) != 1)
		return 1;
	if (checker.takeRRef((int&&)x) != 2)
		return 2;

	int&& rr = (int&&)x;
	if (checker.takeLRef(rr) != 1)
		return 3;

	short s = 7;
	if (checker.takeValue(s) != 4)
		return 4;

	if (checker.takeConstDouble(3) != 3)
		return 5;

	int y = 3;
	if (checker.takeConstDouble(y) != 3)
		return 6;

	return 0;
}
