// Exact primitive operator= overloads should use the actual RHS base type.

struct Target {
	int which;

	Target() : which(0) {}

	void operator=(int) {
		which = 1;
	}

	void operator=(double) {
		which = 2;
	}
};

int main() {
	Target value;
	value = 7;
	if (value.which != 1) return 1;

	value = 3.5;
	return value.which == 2 ? 0 : 2;
}