struct Record {
	double value;
};

struct Widths {
	float narrow;
	long long wide;
};

int runtimeNine() {
	return 9;
}

int runtimeNegative() {
	return -9;
}

struct Partial {
	int tag;
	double value = 9;
	float narrow = 9;
	long long wide = -9;
	double from_call = runtimeNine();
};

int main() {
	Record record{9};
	if (record.value != 9.0) {
		return 1;
	}
	int nine = runtimeNine();
	Record from_runtime{nine};
	if (from_runtime.value != 9.0) {
		return 2;
	}
	Widths widths{9, -9};
	if (widths.narrow != 9.0f || widths.wide != -9) {
		return 3;
	}
	int negative = runtimeNegative();
	Widths from_runtime_widths{nine, negative};
	if (from_runtime_widths.narrow != 9.0f || from_runtime_widths.wide != -9) {
		return 4;
	}
	Partial partial{1};
	if (partial.tag != 1 || partial.value != 9.0 || partial.narrow != 9.0f || partial.wide != -9 || partial.from_call != 9.0) {
		return 5;
	}
	return 0;
}
