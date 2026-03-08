struct Counter {
	int base = 5;

	int sumByValue() {
		int extra = 2;
		auto lambda = [=]() {
			return base + extra;
		};
		return lambda();
	}

	int sumByReference() {
		int extra = 2;
		auto lambda = [&]() {
			extra += base;
			return extra;
		};
		return lambda();
	}
};

int main() {
	Counter counter;
	return counter.sumByValue() + counter.sumByReference();
}